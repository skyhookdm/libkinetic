/**
 * Copyright 2013-2020 Seagate Technology LLC.
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

struct kresult_message create_rangekey_message(kmsghdr_t *, kcmdhdr_t *, krange_t *);
kstatus_t extract_keyrange(struct kresult_message *response_msg, krange_t *keyrange_data);

/**
 * ki_range(int ktd, krange_t *kr)
 *
 *  kr		kr_key must contain a fully populated kiovec array
 *		keyrange_val must contain a zero-ed kiovec array of cnt 1
 * 		keyrange_vers and keyrange_verslen are optional
 * 		krange_tag and krange_taglen are optional.
 *		keyrange_ditype is returned by the server, but it should
 * 		have either a 0 or a valid ditype in it to start with
 *
 * The get APIs share about 95% of the same code. This routine Consolidates
 * the code.
 *
 */
kstatus_t
ki_range(int ktd, krange_t *kr)
{
	int rc, i, n;             // numeric return codes
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

	clock_t clock_rangestart = clock();

	// Get KTLI config
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		krc = (kstatus_t) {
			.ks_code    = K_EREJECTED,
			.ks_message = "Bad session",
			.ks_detail  = "",
		};
		goto rex1;
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	/* Validate the passed in kr */
	if (!kr) {
		krc = (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Missing Parameters",
			.ks_detail  = "",
		};
		goto rex1;
	}
	
	/* If kr_count is set to infinity, fix it */
	if (kr->kr_count == KVR_COUNT_INF)
		kr->kr_count = ses->ks_l.kl_rangekeycnt;

	clock_t clock_validatestart = clock();
	
	/* validate the input keyrange */
	rc = ki_validate_range(kr, &ses->ks_l);
	if (rc < 0) {
		krc = (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Invalid KV",
			.ks_detail  = "",
		};
		goto rex1;
	}

	clock_t clock_validateend = clock();

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		krc = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
		goto rex3;
	}
	memset(kio, 0, sizeof(struct kio));

	/* Setup the KIO */
	kio->kio_cmd = KMT_GETRANGE;
	kio->kio_flags		= KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP);		/* Normal RPC */

	/* 
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See message.h for more details.
	 */
	kio->kio_sendmsg.km_cnt = 2;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		krc =  (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
		goto rex4;
	}

	// hang the Packed PDU buffer, packing occurs later
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

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
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_GETRANGE;

	clock_t clock_reqstart = clock();

	kmreq = create_rangekey_message(&msg_hdr, &cmd_hdr, kr);
	if (kmreq.result_code == FAILURE) {
		krc   = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to construct kinetic message for request",
			.ks_detail  = "",
		};

		goto rex5;
	}

	clock_t clock_reqend = clock();

	// pack the message and hang it on the kio
	// PAK: Error handling
	// success: rc = 0; failure: rc = 1 (see enum kresult_code)
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		krc   = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to pack kinetic message for request",
			.ks_detail  = "",
		};

		goto rex6;
	}

	clock_t clock_packend = clock();

	// now that the message length is known, setup the PDU
	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;

	PACK_PDU(&pdu, ppdu);
	debug_printf("ki_range: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	clock_t clock_send = clock();

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
			else { ; }
		}

		// Got our response
		else { break; }
	} while (1);

	clock_t clock_recv = clock();

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		/* PAK: error handling -need to clean up Yikes! */
		assert(0);
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		/* PAK: error handling -need to clean up Yikes! */
		assert(0);
	}

	/* Now unpack the message */ 
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		krc   = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to unpack kinetic message from response",
			.ks_detail  = "",
		};

		// cleanup and return error
		goto rex7;
	}

	clock_t clock_unpack = clock();

	krc = extract_keyrange(&kmresp, kr);

	clock_t clock_extract = clock();

	/* clean up */
 rex8:
	destroy_message(kmresp.result_message);

 rex7:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that 
	 * was made in kmreq before freeingcalling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
 rex6:
	destroy_message(kmreq.result_message);
 rex5:
	KI_FREE(kio->kio_sendmsg.km_msg);
 rex4:
	KI_FREE(kio);
 rex3:
	if (freeend) {
		ki_keyfree(kr->kr_end, kr->kr_endcnt);
		kr->kr_end = NULL;
		kr->kr_endcnt = 0;
	}
 rex2:			
	if (freestart) {
		ki_keyfree(kr->kr_start, kr->kr_startcnt);
		kr->kr_start = NULL;
		kr->kr_startcnt = 0;
	}

 rex1:
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
	int extract_startkey_result = keyname_to_proto(
		&(proto_cmd_body.startkey),
		cmd_data->kr_start, cmd_data->kr_startcnt
	);
	proto_cmd_body.has_startkey = extract_startkey_result;

#if 0
	if (extract_startkey_result == 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}
#endif
	int extract_endkey_result = keyname_to_proto(
		&(proto_cmd_body.endkey),
		cmd_data->kr_end, cmd_data->kr_endcnt
	);
	proto_cmd_body.has_endkey = extract_endkey_result;

#if 0
	if (extract_endkey_result == 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}
#endif
	
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

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

void destroy_protobuf_keyrange(krange_t *keyrange_data) {
	if (!keyrange_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_keyrange_t *) keyrange_data->keyrange_protobuf);

	// then free arrays of pointers that point to the message data
	KI_FREE(keyrange_data->kr_keys);

	// free the struct itself last
	// NOTE: we may want to leave this to a caller?
	KI_FREE(keyrange_data);
}

kstatus_t extract_keyrange(struct kresult_message *response_msg, krange_t *keyrange_data) {
	// assume failure status
	kstatus_t keyrange_status = (kstatus_t) {
		.ks_code    = K_INVALID_SC,
		.ks_message = NULL,
		.ks_detail  = NULL,
	};

	// commandbytes should exist, but we should probably be thorough
	kproto_msg_t *keyrange_response_msg = (kproto_msg_t *) response_msg->result_message;
	if (!keyrange_response_msg->has_commandbytes) { return keyrange_status; }

	// unpack the command bytes
	kproto_cmd_t *response_cmd = unpack_kinetic_command(keyrange_response_msg->commandbytes);

	// extract the response status to be returned.
	// prepare this early to make cleanup easy
	keyrange_status = extract_cmdstatus(response_cmd);

	if (response_cmd->body == NULL) { return keyrange_status; }
	if (response_cmd->body->range == NULL) { return keyrange_status; }

	// ------------------------------
	// begin extraction of command body into krange_t structure
	kproto_keyrange_t *response = response_cmd->body->range;

	keyrange_data->kr_keyscnt = response->n_keys;
	keyrange_data->kr_keys    = (struct kiovec *) KI_MALLOC(sizeof(struct kiovec) * response->n_keys);

	for (size_t result_keyndx = 0; result_keyndx < response->n_keys; result_keyndx++) {
		keyrange_data->kr_keys[result_keyndx].kiov_base = response->keys[result_keyndx].data;
		keyrange_data->kr_keys[result_keyndx].kiov_len  = response->keys[result_keyndx].len;
	}

	// set fields used for cleanup
	keyrange_data->keyrange_protobuf = response_cmd;
	keyrange_data->destroy_protobuf  = destroy_protobuf_keyrange;

	return keyrange_status;
}
