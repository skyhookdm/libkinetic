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
#include "getlog.h"
#include "message.h"

struct kresult_message create_getkey_message(kmsghdr_t *, kcmdhdr_t *, kv_t *);

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
	int i;
	size_t len;
	//int util, temp, cap, conf, stat, mesg, lim, log;

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
	switch (kv->kv_ditype) {
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
 * g_get_generic(int ktd, kv_t *kv, kv_t *altkv, kmtype_t msg_type)
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
g_get_generic(int ktd, kv_t *kv,  kv_t *altkv, kmtype_t msg_type)
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

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = K_EREJECTED,
			.ks_message = "Bad session",
			.ks_detail  = "",
		};		
	}
	ses = (ksession_t *)cf->kcfg_pconf;
	
	/* Validate command */
	switch (msg_type) {
	case KMT_GETNEXT:
	case KMT_GETPREV:
		if (!altkv)
			return (kstatus_t) {
				.ks_code    = K_EINVAL,
				.ks_message = "Missing Parameters",
				.ks_detail  = "",
			};
		/* no break here, all cmds need a kv ptr, so fall through */
		
	case KMT_GET:
	case KMT_GETVERS:
		if (!kv)
			return (kstatus_t) {
				.ks_code    = K_EINVAL,
				.ks_message = "Missing Parameters",
				.ks_detail  = "",
			};
		break;

	default:
		return (kstatus_t) {
			.ks_code    = K_EREJECTED,
			.ks_message = "Bad Command",
			.ks_detail  = "",
		};
	}
	
	/* Validate the passed in kv and if necessary the altkv */
	rc = ki_validate_kv(kv, &ses->ks_l);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = K_EINVAL,
			.ks_message = "Invalid KV",
			.ks_detail  = "",
		};
	}

	if (altkv) {
		rc = ki_validate_kv(altkv, &ses->ks_l);
		if (rc < 0) {
			return (kstatus_t) {
				.ks_code    = K_EINVAL,
				.ks_message = "Invalid KV",
				.ks_detail  = "",
			};

		}
	}
		
	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL,
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
			.ks_code    = K_EINTERNAL,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}

	/* Hang the Packed PDU buffer, packing occurs later */
	kio->kio_cmd = msg_type;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len = KP_PLENGTH;

	/* Setup msg_hdr */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hmac;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type  = msg_type;

	kmreq = create_getkey_message(&msg_hdr, &cmd_hdr, kv);
	if (kmreq.result_code == FAILURE) {
		goto gex2;
	}

	/* pack the message and hang it on the kio */
	/* PAK: Error handling */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	rc = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[1].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu, ppdu);
	printf("get_generic: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	ktli_send(ktd, kio);
	printf ("Sent Kio: %p\n", kio);

	/* Wait for the response */
	do {
		/* wait for something to come in */
		ktli_poll(ktd, 0);

		/* Check to see if it our response */
		rc = ktli_receive(ktd, kio);
		if (rc < 0)
			if (errno == ENOENT)
				/* Not our response, so try again */
				continue;
			else {
				/* PAK: need to exit, receive failed */
			}
		else
			/* Got our response */
			break;
	} while (1);

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		/* PAK: error handling -need to clean up Yikes! */
		assert(0);
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len ) {
		/* PAK: error handling -need to clean up Yikes! */
		assert(0);
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto gex2;
	}

	/* 
	 * Grab the value and hang it on either the kv or altkv as approriate
	 * Also extract the kv data from response
	 */
	switch (msg_type) {
	case KMT_GET:
		kv->kv_val[0].kiov_base = kiov->kiov_base + rpdu.kp_msglen;
		kv->kv_val[0].kiov_len  = rpdu.kp_vallen;
		krc = extract_getkey(&kmresp, kv);
		if (krc.ks_code != K_OK) {
			rc = -1;
			goto gex1;
		}
		break;
		
	case KMT_GETNEXT:
	case KMT_GETPREV:
		altkv->kv_val[0].kiov_base = kiov->kiov_base + rpdu.kp_msglen;
		altkv->kv_val[0].kiov_len  = rpdu.kp_vallen;
		krc = extract_getkey(&kmresp, altkv);
		if (krc.ks_code != K_OK) {
			rc = -1;
			goto gex1;
		}
		break;

	case KMT_GETVERS:
		krc = extract_getkey(&kmresp, kv);
		if (krc.ks_code != K_OK) {
			rc = -1;
			goto gex1;
		}
		break;
		
	default:
		printf("get: should not get here\n");
		break;
	}

#if 0
	/* PAK: Altkv may need to be validated as well */
	rc = ki_validate_kv(kv);
	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}
#endif

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
ki_get(int ktd, kv_t *key)
{
	return(g_get_generic(ktd, key, NULL, KMT_GET));
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
 *  next	returned key value pair
 *		kv_key must contain a zero-ed kiovec array of cnt 1
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 *
 * Get the key value that follows the given key, tag, and version in kv. 
 *
 */
kstatus_t
ki_getnext(int ktd, kv_t *key, kv_t *next)
{
	return(g_get_generic(ktd, key, next, KMT_GETNEXT));
}

/**
 * ki_getprev(int ktd, kv_t *key, kv_t *prev)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *  prev	returned key value pair
 *		kv_key must contain a zero-ed kiovec array of cnt 1
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 *
 * Get the key value that is prior the given key, tag, and version in kv. 
 *
 */
kstatus_t
ki_getprev(int ktd, kv_t *key, kv_t *prev)
{
	return(g_get_generic(ktd, key, prev, KMT_GETPREV));
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
	return(g_get_generic(ktd, key, NULL, KMT_GETVERS));
}

/*
 * Helper functions
 */
struct kresult_message create_getkey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, kv_t *cmd_data) {

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
	/*
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
			set_primitive_optional((&proto_cmd_body), metadataonly, 1);

		default:
			msgtype_keyval = KMT_GET;
			break;
	}
	*/

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, (void *) &proto_cmd_body, cmd_hdr->kch_type
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	free(proto_cmd_body.key.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

// This may get a partially defined structure if we hit an error during the construction.
void destroy_protobuf_getkey(kv_t *kv_data) {
	// Don't do anything if we didn't get a valid pointer
	if (!kv_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_kv_t *) kv_data->kv_protobuf);

	// then free arrays of pointers that point to the message data

	// free the struct itself last
	// NOTE: we may want to leave this to a caller?
	free(kv_data);
}

kstatus_t extract_getkey(struct kresult_message *response_msg, kv_t *kv_data) {
	// assume failure status
	kstatus_t kv_status = (kstatus_t) {
		.ks_code    = K_INVALID_SC,
		.ks_message = NULL,
		.ks_detail  = NULL,
	};

	// commandbytes should exist, but we should probably be thorough
	kproto_msg_t *kv_response_msg = (kproto_msg_t *) response_msg->result_message;
	if (!kv_response_msg->has_commandbytes) { return kv_status; }

	// unpack the command bytes
	kproto_cmd_t *response_cmd = unpack_kinetic_command(kv_response_msg->commandbytes);
	if (response_cmd->body->keyvalue == NULL) { return kv_status; }

	// extract the response status to be returned. prepare this early to make cleanup easy
	kproto_status_t *response_status = response_cmd->status;

	// copy the status message so that destroying the unpacked command doesn't get weird
	if (response_status->has_code) {
		size_t statusmsg_len     = strlen(response_status->statusmessage);
		char *response_statusmsg = (char *) KI_MALLOC(sizeof(char) * statusmsg_len);
		strcpy(response_statusmsg, response_status->statusmessage);

		char *response_detailmsg = NULL;
		if (response_status->has_detailedmessage) {
			response_detailmsg = (char *) KI_MALLOC(sizeof(char) * response_status->detailedmessage.len);
			memcpy(
				response_detailmsg,
				response_status->detailedmessage.data,
				response_status->detailedmessage.len
			);
		}

		kv_status = (kstatus_t) {
			.ks_code    = response_status->code,
			.ks_message = response_statusmsg,
			.ks_detail  = response_detailmsg,
		};
	}

	// ------------------------------
	// begin extraction of command body into kv_t structure
	kproto_kv_t *response = response_cmd->body->keyvalue;

	// we set the number of keys to 1, since this is not a range request
	kv_data->kv_keycnt = 1;

	// extract key name, db version, tag, and data integrity algorithm
	extract_bytes_optional(kv_data->kv_key->kiov_base, kv_data->kv_key->kiov_len , response, key);

	extract_bytes_optional(kv_data->kv_dbvers, kv_data->kv_dbverslen, response, dbversion);
	extract_bytes_optional(kv_data->kv_tag   , kv_data->kv_taglen   , response, tag      );

	extract_primitive_optional(kv_data->kv_ditype, response, algorithm);

	// set fields used for cleanup
	kv_data->kv_protobuf      = response_cmd;
	kv_data->destroy_protobuf = destroy_protobuf_getkey;

	return kv_status;
}
