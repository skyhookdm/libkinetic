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
ki_validate_kv(kv_t *kv, limit_t *lim)
{
	int i;
	size_t len;
	int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */

	
	/* Check the required key */
	if (!kv || !kv->kv_key || kv->kv_keycnt < 1) 
		return(-1);

	/* Total up the length across all vectors */
	for (len=0, i=0; i<kv->kv_keycnt; i++)
		len += kv->kv_key[i].kiov_len;

	if (len > lim->kl_keylen)
		return(-1);

	/* Check the value vectors */	
	if (!kv->kv_val || kv->kv_valcnt < 1) 
		return(-1);

	/* Total up the length across all vectors */
	for (len=0, i=0; i<kv->kv_valcnt; i++)
		len += kv->kv_val[i].kiov_len;

	if (len > lim->kl_vallen)
		return(-1);
	
	/* Check the version */
	if (kv->kv_vers &&
	    ((kv->kv_verslen < 1) || (kv->kv_verslen > lim->kl_verlen)))
		return(-1);

	/* Check the tag */
	if (kv->kv_tag &&
	    ((kv->kv_taglen < 1) || (kv->kv_taglen > lim->kl_taglen)))
		return(-1);

	/* Check the Data integrity Type */
	switch (kv->kvditype) {
	case 0: /* zero is unused by the protocol but is valid for gets */
	case KDI_SHA1:
	case KDI_SHA2:
	case KDI_SHA3:
	case KDI_CRC32C:
	case KDI_CRC64:
	case KDI_CRC32:
		break;
	default:
		return(-1);
	}
		
	errno = 0;
	return(0);
}

/**
 * g_get_generic(int ktd, kv_t *kv, kv_t *altkv, uint32_t cmd)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *  altkv	used for holding prev or next kv
 *  cmd		Can be KMT_GET, KMT_GETNEXT, KMT_GETPREV, KMT_GETVERS
 *
 * The get APIs share about 95% of the same code. This routine Consolidates
 * the code.
 *
 */
kstatus_t
g_get_generic(int ktd, kv_t *kv,  kv_t *altkv, kmtype_t cmd)
{
	int rc, n;
	kstatus_t krc;
	struct kio *kio;
	struct ktli_config *cf;
	kpdu_t pdu;
	kpdu_t *rpdu;
	kmsghdr_t msg_hdr;
	kcmdhdr_t cmd_hdr;
	ksession_t *ses;
	struct kresult_message kmreq, kmresp;

	/* Get KTLI config */
	rc = ktli_conf(ktd, *cf);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = K_REJECTED,
			.ks_message = "Bad session",
			.ks_detail  = "",
		};		
	}
	ses = ( ksession_t)cf->kcfg_pconf;
	
	/* Validate the passed in kv */
	rc = ki_validate_kv(kv, &ses->ks_l);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = errno,
			.ks_message = "Invalid KV",
			.ks_detail  = "",
		};
	}

	/* Clear altkv, data will come from the server */
	memset((void *)altkv, 0, sizeof(kv_t));

	/* Validate command */
	switch (cmd) {
	case KMT_GET:
	case KMT_GETNEXT:
	case KMT_GETPREV:
	case KMT_GETVERS:
		break;
	default:
		return (kstatus_t) {
			.ks_code    = K_REJECTED,
			.ks_message = "Bad Command",
			.ks_detail  = "",
		};
	}
		
	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL;
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}
	memset(kio, 0, sizeof(struct kio));

	/* 
	 * Allocate kio vectors array of size 2
	 * One vector for the PDU and another for the full request message
	 */
	kio->kio_sendmsg.km_cnt = 2; 
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL;
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}

	/* Hang the PDU buffer */
	kio->kio_cmd = cmd;
	kio->kio_sendmsg.km_msg[0].kiov_base = (void *) &pdu;
	kio->kio_sendmsg.km_msg[0].kiov_len = KP_LENGTH;

	/* Setup msg_hdr */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hmac;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type  = cmd;

	kmreq = create_get_message(&msg_hdr, &cmd_hdr, kv);
	if (kmreq.result_code == FAILURE) {
		goto gex2;
	}

	/* pack the message and hang it on the kio */
	/* PAK: Error handling */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	rc = pack_kinetic_message(
		(kproto_msg_t *) &(kmreq.result_message),
		&(kio->kio_sendmsg.km_msg[1].kiov_base),
		&(kio->kio_sendmsg.km_msg[1].kiov_len)
	);

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[1].kiov_len;
	pdu.kp_vallen = 0;

	/* Send the request */
	ktli_send(ktd, kio);
	printf ("Sent Kio: %p\n", kio);

	/* Wait for the response */
	ktli_poll(ktd, 0);

	/* Receive the response */
	/* PAK: need error handling */
	rc = ktli_receive(ktd, kio);

	/* extract the return PDU */
	if (kio->kio_recvmsg.km_msg[0].kiov_len != sizeof(pdu_t)) {
		/* PAK: error handling -need to clean up Yikes! */
	}
	rpdu = kio->kio_recvmsg.km_msg[0].kiov_base;

	/* Does the PDU match what was given in the recvmsg */
	if (rpdu->kp_msglen + rpdu->kp_vallen !=
	    kio->kio_recvmsg.km_msg[1].kiov_len ) {
		/* PAK: error handling -need to clean up Yikes! */
	}

	/* Grab the value */
	kv->kv_val = kio->kio_recvmsg.km_msg[1].kiov_base + rpdu->kp_msglen;
	kv->kv_val = rpdu->kp_vallen;

	/* Now unpack the message */ 
	kmresp = unpack_get_resp(kio->kio_recvmsg.km_msg[1].kiov_base,
				 kio->kio_recvmsg.km_msg[1].kiov_len);

	if (kmresp->result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto gex2;
	}

	kstatus = extract_get(kmresp, kv);
	if (kstatus->ks_code != K_OK) {
		rc = -1;
		goto gex1;
	}

	rc = ki_validate_kv(kv);

	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}

	/* clean up */
 gex1:
	destroy_message(kmresp);
 gex2:
	destroy_message(kmreq);
	destroy_request(kio->kio_sendmsg.km_msg[1].kiov_base);

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_recvmsg.km_msg[0].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[1].kiov_base);
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

/**
 * ki_get(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Get the value specified by the given key, tag, and version. 
 *
 */
kstatus_t
ki_get(int ktd, kv_t *key, kv_t *next)
{


}

/**
 * ki_getnext(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Get the key value that follows the given key, tag, and version in kv. 
 *
 */
kstatus_t
ki_getnext(int ktd, kv_t *key, kv_t *next)
{


}

/**
 * ki_getprev(int ktd, kv_t *kv, kv_t *prev)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *  prev	Returned key 
 *
 * Get the key value that is prior the given key, tag, and version in kv. 
 *
 */
kstatus_t
ki_getprev(int ktd, kv_t *key, kv_t *prev)
{

}

/**
 * ki_getversion(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Get the version specified by the given key kv.  Do not return the value.
 *
 */
kstatus_t
ki_getversion(int ktd, kv_t *key)
{



}

/*
 * Helper functions
 */
struct kresult_message create_getkey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
											 kv_t *cmd_data, kvgettype_t get_type) {

	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_kv_t     proto_cmd_body;

	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// GET only needs key name from cmd_data
	int extract_result = keyname_to_proto(&proto_cmd_body, cmd_data);
	if (extract_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	/* GETNEXT and GETPREV are relative to the keyname in this request */
	kmtype_t msgtype_keyval;
	switch (get_type) {
		case GET_TYPE_VERS:
			msgtype_keyval = KMT_GETVERS;
			break;

		case GET_TYPE_NEXT:
			msgtype_keyval = KMT_GETNEXT;
			break;

		case GET_TYPE_PREV:
			msgtype_keyval = KMT_GETPREV;
			break;

		// falls through so that the message type is `GET`
		case GET_TYPE_META:
			set_primitive_optional(&proto_cmd_body, metadataonly, 1);

		default:
			msgtype_keyval = KMT_GET;
			break;
	}

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, (void *) &proto_cmd_body, msgtype_keyval
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	free(proto_cmd_body.key.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}
