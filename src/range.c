/**
 * Copyright 2020-2021 Seagate Technology LLC.
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 * https://mozilla.org/MP:/2.0/.
 *
 * This program is distributed in the hope that it will be useful,
 * but is provided AS-IS, WITHOUT ANY WARRANTY; including without
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
 * License for more details.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>

#include <time.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"
#include "protocol_interface.h"


typedef struct range_ctx {
	kproto_cmd_t *response_cmd;
	krange_t     *keyrange_data;
} range_ctx_t;


void destroy_range_ctx(void *ctx_ptr) {
	// If we have nothing to do; return.
	if (!ctx_ptr) { return; }

	range_ctx_t *ctx_data      = (range_ctx_t *) ctx_ptr;
	krange_t    *keyrange_data = ctx_data->keyrange_data;

	// First, release the allocated memory for the protobuf message data
	if (ctx_data->response_cmd) { destroy_command(ctx_data->response_cmd); }

	// Second, destroy the kiovec array (contains pointers to the list of keys).
	// NOTE: maybe should figure out why kr_keys is free'd a second time
	if (   keyrange_data
	    && keyrange_data->kr_keys
	    && keyrange_data->kr_keys != UNALLOC_VAL) {
		KI_FREE(keyrange_data->kr_keys);
	}

	// Finally, free the ctx pointer, itself
	KI_FREE(ctx_ptr)
}


/**
 * Internal prototypes
 */
struct kresult_message
create_rangekey_message(kmsghdr_t *, kcmdhdr_t *, krange_t *);

/**
 * ki_getrange(int ktd, krange_t *kr)
 *
 *  kr		kr_key must contain a fully populated kiovec array
 *		keyrange_val must contain a zero-ed kiovec array of cnt 1
 * 		keyrange_vers and keyrange_verslen are optional
 * 		krange_tag and krange_taglen are optional.
 *		keyrange_ditype is returned by the server, but it should
 * 		have either a 0 or a valid ditype in it to start with
 *
 */
kstatus_t
ki_getrange(int ktd, krange_t *kr)
{
	int rc, n;              // numeric return codes
	int freestart=0;		/* bools to remember if ki_range */
	int freeend=0;			/* needs to free either start or end */
	kstatus_t krc;            // return code and messages
	struct kio *kio;          // KTLI compliant req and resp
	struct kiovec *kiov;      // shortcut var to reduce line lengths
	struct ktli_config *cf;   // connection configuration
	uint8_t ppdu[KP_PLENGTH]; // packed PDU (protocol data unit)
	kpdu_t pdu;		  // req PDU
	kpdu_t rpdu;              // response PDU
	kmsghdr_t msg_hdr;        // header of a kinetic `Message`
	kcmdhdr_t cmd_hdr;        // header of a kinetic `Command`
	ksession_t *ses;          // reference to the kinetic session
	struct kresult_message kmreq, kmresp;

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_rangestart = clock();
	#endif

	// Get KTLI config
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("range: ktli config\n");
		krc = K_EBADSESS;
		goto rex_end;
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_validatestart = clock();
	#endif

	/* validate the input keyrange */
	rc = ki_validate_range(kr, &ses->ks_l);
	if (rc < 0) {
		debug_printf("range: kv invalid\n");
		krc = K_EINVAL;
		goto rex_end;
	}

	/* If kr_count is set to infinity, fix it */
	if (kr->kr_count == KVR_COUNT_INF)
		kr->kr_count = ses->ks_l.kl_rangekeycnt;

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_validateend = clock();
	#endif

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		debug_printf("range: kio alloc\n");
	        krc = K_ENOMEM;
		goto rex_end;
	}
	memset(kio, 0, sizeof(struct kio));

	/*
	 * Setup msg_hdr
	 * One thing to note here is that although the msg hdr is being setup
	 * it is too early to complete. The msg hdr will ultimately have a
	 * HMAC cryptographic checksum of the requests command bytes, so that
	 * server can authenticate and authorize the request. The command
	 * bytes don't actually get finalized until a ktli_send is initiated.
	 * So for now the HMAC key is hung onto the kmh_hmac field. It will
	 * used later on to calculate the actual HMAC which will then be hung
	 * of the kmh_hmac field. A reference is made to the kcfg_hkey ptr
	 * in the kmreq. This reference needs to be removed before freeing
	 * kmreq. See below at rex2:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KAT_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_GETRANGE;

	/* sequence number gets set during the send */

	kmreq = create_rangekey_message(&msg_hdr, &cmd_hdr, kr);
	if (kmreq.result_code == FAILURE) {
		debug_printf("range: request message create\n");
		krc = K_EINTERNAL;
		goto rex_kio;
	}

	/* Setup the KIO */
	kio->kio_cmd 	= KMT_GETRANGE;
	kio->kio_flags	= KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP);		/* Normal RPC */

	/*
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		debug_printf("range: sendmesg alloc\n");
		krc = K_ENOMEM;
		goto rex_req;
	}

	// hang the Packed PDU buffer, packing occurs later
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_reqstart = clock();
	#endif

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_reqend = clock();
	#endif

	// pack the message and hang it on the kio
	// PAK: Error handling
	// success: rc = 0; failure: rc = 1 (see enum kresult_code)
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("range: sendmesg msg pack\n");
		krc = K_EINTERNAL;
		goto rex_sendmsg;
	}

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_packend = clock();
	#endif

	// now that the message length is known, setup the PDU
	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;

	PACK_PDU(&pdu, ppdu);

	debug_printf("ki_range: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_send = clock();
	#endif

	/* Send the request */
	ktli_send(ktd, kio);
	debug_printf("Sent Kio: %p\n", kio);

	// Wait for the response
	do {
		// wait for something to come in
		ktli_poll(ktd, 0);

		// Check to see if it our response
		rc = ktli_receive(ktd, kio);
		if (rc < 0) {
			/* Not our response, so try again */
			if (errno == ENOENT) { continue; }

			/* PAK: need to exit, receive failed */
			else {
				debug_printf("range: kio receive failed\n");
				krc = K_EINTERNAL;
				goto rex_sendmsg;
			}
		}

		// Got our response
		else { break; }
	} while (1);

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go.
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("range: kio timed out\n");
		krc = K_ETIMEDOUT;
		goto rex_recvmsg;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("range: kio failed\n");
		krc = K_ENOMSG;
		goto rex_recvmsg;
	}

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_recv = clock();
	#endif

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("range: PDU bad length\n");
		krc = K_EINTERNAL;
		goto rex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		debug_printf("range: PDU decode\n");
		krc = K_EINTERNAL;
		goto rex_recvmsg;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("range: msg unpack\n");
		krc = K_EINTERNAL;
		goto rex_resp;
	}

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_unpack = clock();
	#endif

	krc = extract_keyrange(&kmresp, kr);

	#if LOGLEVEL >= LOGLEVEL_DEBUG
	clock_t clock_extract = clock();
	#endif

	/* clean up */
 rex_resp:
	destroy_message(kmresp.result_message);

 rex_recvmsg:
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);

 rex_sendmsg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

 rex_req:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before freeingcalling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);


 rex_kio:
	KI_FREE(kio);

 rex_end:
	if (freeend) {
		ki_keydestroy(kr->kr_end, kr->kr_endcnt);
		kr->kr_end = NULL;
		kr->kr_endcnt = 0;
	}

	if (freestart) {
		ki_keydestroy(kr->kr_start, kr->kr_startcnt);
		kr->kr_start = NULL;
		kr->kr_startcnt = 0;
	}

	debug_printf("Times for ki_range:\n");
	debug_printf("\tTotal           : %lu\n", clock_extract - clock_rangestart);
	debug_printf("\tValidation      : %lu\n", clock_validateend - clock_validatestart);
	debug_printf("\tCreate request  : %lu\n", clock_reqend - clock_reqstart);
	debug_printf("\tPack request    : %lu\n", clock_packend - clock_reqend);
	debug_printf("\tKTLI (send/recv): %lu\n", clock_recv - clock_send);
	debug_printf("\tUnpack response : %lu\n", clock_unpack - clock_recv);
	debug_printf("\tExtract response: %lu\n", clock_extract - clock_unpack);

	return (krc);
}


struct kresult_message
create_rangekey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, krange_t *cmd_data) {
	// declare protobuf structs on stack
	kproto_keyrange_t proto_cmd_body;
	com__seagate__kinetic__proto__command__range__init(&proto_cmd_body);

	// extract from cmd_data into proto_cmd_body

	// >> Get start key
	int extract_startkey_result = keyname_to_proto(
		&(proto_cmd_body.startkey),
		cmd_data->kr_start, cmd_data->kr_startcnt
	);
	proto_cmd_body.has_startkey = extract_startkey_result;

	// >> Get end key
	int extract_endkey_result = keyname_to_proto(
		&(proto_cmd_body.endkey),
		cmd_data->kr_end, cmd_data->kr_endcnt
	);
	proto_cmd_body.has_endkey = extract_endkey_result;

	set_primitive_optional(&proto_cmd_body, startkeyinclusive, KR_ISTART(cmd_data));
	set_primitive_optional(&proto_cmd_body, endkeyinclusive  , KR_IEND(cmd_data));
	set_primitive_optional(&proto_cmd_body, reverse          , KR_REVERSE(cmd_data));
	set_primitive_optional(&proto_cmd_body, maxreturned      , cmd_data->kr_count  );

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(cmd_hdr, &proto_cmd_body);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	KI_FREE(proto_cmd_body.startkey.data);
	KI_FREE(proto_cmd_body.endkey.data);

	// return the constructed range message (or failure)
	return create_message(msg_hdr, command_bytes);
}

kstatus_t extract_keyrange(struct kresult_message *resp_msg, krange_t *kr_data){
	// assume failure status
	kstatus_t krc = K_EINTERNAL;
	kproto_msg_t *kr_resp_msg;

	// commandbytes should exist, but we should probably be thorough
	kr_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!kr_resp_msg->has_commandbytes) {
		debug_printf("extract_keyrange: no resp cmd\n");
		return krc;
	}

	// unpack the command bytes
	kproto_cmd_t *resp_cmd = unpack_kinetic_command(kr_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_keyrange: resp cmd unpack\n");
		return krc;
	}

	// try allocating this first to simplify the error modes;
	// on error, only the unpacked command has been allocated so far.
	range_ctx_t *ctx_pair = (range_ctx_t *) KI_MALLOC(sizeof(range_ctx_t));
	if (!ctx_pair) {
		destroy_command(resp_cmd);
		debug_printf("extract_keyrange: unable to allocate context\n");
		return krc;
	}

	// make sure we don't erroneously try to cleanup garbage pointers
	memset(ctx_pair, 0, sizeof(range_ctx_t));

	// extract the status. On failure, skip to cleanup
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK) {
		debug_printf("extract_keyrange: status\n");
		goto extract_rex;
	}

	if (!resp_cmd->body  || !resp_cmd->body->range) {
		debug_printf("extract_keyrange: command missing body or kv\n");
		goto extract_rex;
	}

	*ctx_pair = (range_ctx_t) {
		 .response_cmd  = resp_cmd
		,.keyrange_data = kr_data
	};

	// Since everything seemed successful, let's pop this data on our cleaning stack
	krc = ki_addctx(kr_data, (void *) ctx_pair, destroy_range_ctx);
	if (krc != K_OK) {
		debug_printf("extract_keyrange: failed to add context\n");
		goto extract_rex;
	}

	// ------------------------------
	// begin extraction of command data
    // NOTE: We do not extract the start or end keys, so those fields are maintained by the caller.
    //       We only need to manage the returned key list.
	// check if there's command data to parse, otherwise cleanup and exit
	kproto_keyrange_t *resp = resp_cmd->body->range;
	size_t kr_keys_bytelen  = sizeof(struct kiovec) * resp->n_keys;

	kr_data->kr_keyscnt = resp->n_keys;
	kr_data->kr_keys    = (struct kiovec *) KI_MALLOC(kr_keys_bytelen);
	if (!kr_data->kr_keys) {
		debug_printf("extract_keyrange: key vector\n");
		krc = K_ENOMEM;
		goto extract_rex;
	}

	for (size_t i = 0; i < resp->n_keys; i++) {
		kr_data->kr_keys[i].kiov_base = resp->keys[i].data;
		kr_data->kr_keys[i].kiov_len  = resp->keys[i].len;
	}

	return krc;

 extract_rex:
	debug_printf("extract_keyrange: error exit\n");
	destroy_range_ctx(ctx_pair);

	// Just make sure we don't return an ok message
	if (krc == K_OK) { krc = K_EINTERNAL; }

	return krc;
}
