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

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"

struct kresult_message create_rangekey_message(kmsghdr_t *, kcmdhdr_t *, kr_t *);
kstatus_t extract_keyrange(struct kresult_message *response_msg, kr_t *keyrange_data);

/**
 * ki_validate_range(kr_t *keyrange, limit_t *lim)
 *
 *  keyrange		Always contains a start key and end key.
 *  lim 	Contains server limits
 *
 * Validate that the user is passing a valid keyrange structure
 *
 */
int
ki_validate_range(kr_t *keyrange, klimits_t *lim)
{
	/* assume we will find a problem */
	errno = K_EINVAL;

	/* Check for required params. This is _True_ on _error_  */
	int bool_has_invalid_params = (
		   !keyrange                                              // kr_t struct
		|| !keyrange->kr_startkey || keyrange->kr_startkeycnt < 1 // start key and its length
		|| !keyrange->kr_endkey   || keyrange->kr_endkeycnt   < 1 // end key and its length
		|| keyrange->kr_max_keylistcnt > lim->kl_rangekeycnt      // requested key count
	);

	// check that the required params were provided and had valid values
	if (bool_has_invalid_params) { return (-1); }

	/* Check key name and key value lengths are not too long */
	size_t total_startkey_len = calc_total_len(keyrange->kr_startkey, keyrange->kr_startkeycnt);
	if (total_startkey_len > lim->kl_keylen) { return(-1); }

	/* Total up the length across all vectors */
	size_t total_endkey_len = calc_total_len(keyrange->kr_endkey, keyrange->kr_endkeycnt);
	if (total_endkey_len > lim->kl_keylen) { return(-1); }

	errno = 0;

	return (0);
}

/**
 * ki_range(int ktd, kr_t *keyrange)
 *
 *  keyrange		keyrange_key must contain a fully populated kiovec array
 *		keyrange_val must contain a zero-ed kiovec array of cnt 1
 * 		keyrange_vers and keyrange_verslen are optional
 * 		kr_tag and kr_taglen are optional.
 *		keyrange_ditype is returned by the server, but it should
 * 		have either a 0 or a valid ditype in it to start with
 *
 * The get APIs share about 95% of the same code. This routine Consolidates
 * the code.
 *
 */
kstatus_t
ki_range(int ktd, kr_t *keyrange)
{
	int rc;                   // numeric return codes
	kstatus_t krc;            // structured return code and messages

	struct kio *kio;          // structure containing all relevant request/response data
	struct ktli_config *cf;   // configuration associated with a connection

	uint8_t ppdu[KP_PLENGTH]; // byte array holding the packed PDU (protocol data unit)
	kpdu_t rpdu;              // structure to hold the response PDU in
	kmsghdr_t msg_hdr;        // header of a kinetic `Message`
	kcmdhdr_t cmd_hdr;        // header of a kinetic `Command`
	ksession_t *ses;          // reference to the kinetic session

	// results of creating (kmreq) a request message or unpacking (kmresp) a response message
	struct kresult_message kmreq, kmresp;

	/* Prep and validation */

	// Get KTLI config
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = K_EREJECTED,
			.ks_message = "Bad session",
			.ks_detail  = "",
		};
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	// validate command
	if (!keyrange) {
		errno = K_EINVAL;
		return (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Missing Parameters",
			.ks_detail  = "",
		};
	}

	// validate the input keyrange
	rc = ki_validate_range(keyrange, &ses->ks_l);
	if (rc < 0) {
		errno = K_EINVAL;
		return (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Invalid KV",
			.ks_detail  = "",
		};
	}

	// create the kio structure
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		errno = K_EINTERNAL;
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}
	memset(kio, 0, sizeof(struct kio));

	/* Prepare request data */

	// Allocate kio vectors array of size 2 (PDU, full request message)
	kio->kio_cmd            = KMT_GETRANGE;
	kio->kio_sendmsg.km_cnt = KIO_LEN_NOVAL;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(sizeof(struct kiovec) * KIO_LEN_NOVAL);

	if (!kio->kio_sendmsg.km_msg) {
		errno = K_EINTERNAL;
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
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
	// setup message header (msg_hdr)
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	// setup command header (cmd_hdr)
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_GETRANGE;

	kmreq = create_rangekey_message(&msg_hdr, &cmd_hdr, keyrange);
	if (kmreq.result_code == FAILURE) {
		errno = K_EINTERNAL;
		krc   = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to construct kinetic message for request",
			.ks_detail  = "",
		};

		goto rex2;
	}

	// pack the message and hang it on the kio
	// PAK: Error handling
	// success: rc = 0; failure: rc = 1 (see enum kresult_code)
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);
	if (pack_result == FAILURE) {
		errno = K_EINTERNAL;
		krc   = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to pack kinetic message for request",
			.ks_detail  = "",
		};

		goto rex2;
	}

	// now that the message length is known, setup the PDU
	kpdu_t pdu = {
		.kp_magic  = KP_MAGIC,
		.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len,
		.kp_vallen = 0,
	};
	PACK_PDU(&pdu, ppdu);

	printf(
		"get_generic: PDU(x%2x, %d, %d)\n",
	    pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen
	);


	/* Communication over the network */

	// Send the request
	ktli_send(ktd, kio);
	printf("Sent Kio: %p\n", kio);

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


	/* Parse response */

	// extract the return PDU
	struct kiovec *kiov_rpdu = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov_rpdu->kiov_len != KP_PLENGTH) {
		/* PAK: error handling -need to clean up Yikes! */
		assert(0);
	}

	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov_rpdu->kiov_base)));

	// validate the PDU (does it match what was given in the recvmsg)
	struct kiovec *kiov_rmsg = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov_rmsg->kiov_len ) {
		// PAK: error handling -need to clean up Yikes!
		assert(0);
	}

	// Now unpack the message
	kmresp = unpack_kinetic_message(kiov_rmsg->kiov_base, kiov_rmsg->kiov_len);
	if (kmresp.result_code == FAILURE) {
		errno = K_EINTERNAL;
		krc   = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to unpack kinetic message from response",
			.ks_detail  = "",
		};

		// cleanup and return error
		goto rex1;
	}

	// NOTE: extract the status to be propagated, and then cleanup; no goto needed
	krc = extract_keyrange(&kmresp, keyrange);
	// if (krc.ks_code != K_OK) { goto rex1; }


	/* clean up */
 rex1:
	destroy_message(kmresp.result_message);

 rex2:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that 
	 * was made in kmreq before freeingcalling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);

	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

	KI_FREE(kio);

	return (krc);
}


struct kresult_message create_rangekey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, kr_t *cmd_data) {
	// declare protobuf structs on stack
	kproto_cmdhdr_t   proto_cmd_header;
	kproto_keyrange_t proto_cmd_body;

	com__seagate__kinetic__proto__command__range__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// extract from cmd_data into proto_cmd_body
	int extract_startkey_result = keyname_to_proto(
		&(proto_cmd_body.startkey), cmd_data->kr_startkey, cmd_data->kr_startkeycnt
	);
	proto_cmd_body.has_startkey = extract_startkey_result;

	if (extract_startkey_result == 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	int extract_endkey_result = keyname_to_proto(
		&(proto_cmd_body.endkey), cmd_data->kr_endkey, cmd_data->kr_endkeycnt
	);
	proto_cmd_body.has_endkey = extract_endkey_result;

	if (extract_endkey_result == 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	set_primitive_optional(&proto_cmd_body, startkeyinclusive, cmd_data->kr_bool_is_start_inclusive);
	set_primitive_optional(&proto_cmd_body, endkeyinclusive  , cmd_data->kr_bool_is_end_inclusive  );
	set_primitive_optional(&proto_cmd_body, reverse          , cmd_data->kr_bool_reverse_keyorder  );
	set_primitive_optional(&proto_cmd_body, maxreturned      , cmd_data->kr_max_keylistcnt         );

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(&proto_cmd_header, &proto_cmd_body);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	KI_FREE(proto_cmd_body.startkey.data);
	KI_FREE(proto_cmd_body.endkey.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

void destroy_protobuf_keyrange(kr_t *keyrange_data) {
	if (!keyrange_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_keyrange_t *) keyrange_data->keyrange_protobuf);

	// then free arrays of pointers that point to the message data
	KI_FREE(keyrange_data->kr_result_keylist);

	// free the struct itself last
	// NOTE: we may want to leave this to a caller?
	KI_FREE(keyrange_data);
}

kstatus_t extract_keyrange(struct kresult_message *response_msg, kr_t *keyrange_data) {
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
	if (response_cmd->body->range == NULL) { return keyrange_status; }

	// extract the response status to be returned. prepare this early to make cleanup easy
	keyrange_status = extract_cmdstatus(response_cmd);

	// check if there's any range information to parse
	if (response_cmd->body->range == NULL) { return keyrange_status; }

	// ------------------------------
	// begin extraction of command body into kr_t structure
	kproto_keyrange_t *response = response_cmd->body->range;

	keyrange_data->kr_result_keylistcnt = response->n_keys;
	keyrange_data->kr_result_keylist    = (struct kiovec *) KI_MALLOC(sizeof(struct kiovec) * response->n_keys);

	for (size_t result_keyndx = 0; result_keyndx < response->n_keys; result_keyndx++) {
		keyrange_data->kr_result_keylist[result_keyndx].kiov_base = response->keys[result_keyndx].data;
		keyrange_data->kr_result_keylist[result_keyndx].kiov_len  = response->keys[result_keyndx].len;
	}

	// set fields used for cleanup
	keyrange_data->keyrange_protobuf = response_cmd;
	keyrange_data->destroy_protobuf  = destroy_protobuf_keyrange;

	return keyrange_status;
}
