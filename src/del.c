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

struct kresult_message
create_delkey_message(kmsghdr_t *, kcmdhdr_t *, kv_t *, kcachepolicy_t, int);


/**
 * ki_validate_kv(kv_t *kv, limit_t *lim)
 *
 *  kv		Always contains a key, but optionally may have a value
 *		However there must always value kiovec array of at least
 * 		element and should always be initialized. if unused,
 *		as in a get, kv_val[0].kiov_base=NULL and kv_val[0].kiov_len=0
 *  lim 	Contains server limits
 *
 * Validate that the user is passing a valid kv structure
 *
 */
int
ki_validate_kv(kv_t *kv, klimits_t *lim)
{
	/* assume we will find a problem */
	errno = K_EINVAL;

	/* Check for required params. This is _True_ on _error_  */
	int bool_has_invalid_params = (
		   !kv                                 // kv_t struct
		|| !kv->kv_key || kv->kv_keycnt < 1    // key name
		|| !kv->kv_val || kv->kv_valcnt < 1    // key value
		|| !kv->kv_dbvers                      // db version
		|| kv->kv_dbverslen < 1                // db version is not too short
		|| kv->kv_dbverslen > lim->kl_verlen   // db version is not too long
	);

	// db version must be provided, and must be a valid length
	if (bool_has_invalid_params) { return (-1); }

	/* Check key name and key value lengths are not too long */
	size_t total_key_len = calc_total_len(kv->kv_key, kv->kv_keycnt);
	if (total_key_len > lim->kl_keylen) { return(-1); }

	/* Total up the length across all vectors */
	size_t total_val_len = calc_total_len(kv->kv_val, kv->kv_valcnt);
	if (total_val_len > lim->kl_vallen) { return(-1); }

	errno = 0;

	return(0);
}


/**
 * ki_del(int ktd, kv_t *kv, kv_t *altkv, kmtype_t msg_type)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional.
 *		kv_ditype is returned by the server, but it should
 * 		have either a 0 or a valid ditype in it to start with
 *  altkv	 used for holding prev or next kv
 *  msg_type Can be KMT_GET, KMT_GETNEXT, KMT_GETPREV, KMT_GETVERS
 *
 * The get APIs share about 95% of the same code. This routine Consolidates
 * the code.
 *
 */
kstatus_t
ki_del(int ktd, kv_t *kv)
{
	int rc, n;
	kstatus_t krc;

	struct kio *kio;
	struct kiovec *kiov;
	struct ktli_config *cf;

	uint8_t ppdu[KP_PLENGTH];

	kpdu_t pdu;
	kpdu_t rpdu;

	kmsghdr_t msg_hdr;
	kcmdhdr_t cmd_hdr;
	ksession_t *ses;
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
	if (!kv) {
		return (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Missing Parameters",
			.ks_detail  = "",
		};
	}

	// validate the input kv
	rc = ki_validate_kv(kv, &ses->ks_l);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Invalid KV",
			.ks_detail  = "",
		};
	}

	// create the kio structure
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}
	memset(kio, 0, sizeof(struct kio));

	/* Prepare request data */

	// Allocate kio vectors array of size 2 (PDU, full request message)
	kio->kio_sendmsg.km_cnt = 2;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(
		sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	);

	if (!kio->kio_sendmsg.km_msg) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}

	// hang the Packed PDU buffer, packing occurs later
	kio->kio_cmd = msg_type;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

	// setup message header (msg_hdr)
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hmac;

	// setup command header (cmd_hdr)
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type  = msg_type;

	kmreq = create_getkey_message(&msg_hdr, &cmd_hdr, kv, cmd_hdr.kch_type);
	if (kmreq.result_code == FAILURE) {
		goto gex2;
	}

	// pack the message and hang it on the kio
	// PAK: Error handling
	// success: rc = 0; failure: rc = 1 (see enum kresult_code)
	rc = pack_kinetic_message(
		(kproto_msg_t *) &(kmreq.result_message),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	// now that the message length is known, setup the PDU
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[1].kiov_len;
	pdu.kp_vallen = 0;
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
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		/* PAK: error handling -need to clean up Yikes! */
		assert(0);
	}

	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	// validate the PDU (does it match what was given in the recvmsg)
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len ) {
		// PAK: error handling -need to clean up Yikes!
		assert(0);
	}

	// Now unpack the message
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		// cleanup and return error
		rc = -1;
		goto gex2;
	}

	// Free the status messages since we don't propagate them
	if (krc.ks_message != NULL) { KI_FREE(krc.ks_message); }
	if (krc.ks_detail  != NULL) { KI_FREE(krc.ks_detail);  }

	return (kstatus_t) {
		.ks_code    = K_OK,
		.ks_message = "Success",
		.ks_detail  = "",
	};


	/* clean up */
 gex1:
	destroy_message(kmresp.result_message);
 gex2:
	destroy_message(kmreq.result_message);
	destroy_message(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);
	KI_FREE(kio->kio_sendmsg.km_msg);
	KI_FREE(kio);

	// TODO: translae rc error code into kstatus_t
	// return (rc);
	return (kstatus_t) {
		.ks_code    = K_INVALID_SC,
		.ks_message = "Error sending getlog request: glex2",
		.ks_detail  = "",
	};
}


struct kresult_message create_delkey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
											 kv_t *cmd_data, kcachepolicy_t cache_opt,
											 int bool_shouldforce) {

	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_kv_t     proto_cmd_body;

	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// extract from cmd_data into proto_cmd_body
	int extract_result = keyname_to_proto(&proto_cmd_body, cmd_data);
	if (extract_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// to delete we need to specify the dbversion
	set_bytes_optional(&proto_cmd_body, dbversion, cmd_data->kv_dbvers, cmd_data->kv_dbverslen);

	// if force is specified, then the dbversion is essentially ignored.
	set_primitive_optional(&proto_cmd_body, force          , bool_shouldforce ? 1 : 0);
	set_primitive_optional(&proto_cmd_body, synchronization, cache_opt               );

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, &proto_cmd_body, KMT_DEL
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	free(proto_cmd_body.key.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}
