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
 * Internal prototypes
 */
struct kresult_message create_getkey_message(kmsghdr_t *, kcmdhdr_t *, kv_t *);

/**
 * g_get_generic(int ktd, kv_t *kv, kv_t *altkv, kmtype_t msg_type)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_disum and kv_disumlen are optional. 
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
	int rc, n, force;
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
	if (rc < 0) { return kstatus_err(K_EREJECTED, KI_ERR_BADSESS, "get: ktli config"); }

	ses = (ksession_t *) cf->kcfg_pconf;
	
	/* Validate command */
	switch (msg_type) {
	case KMT_GETNEXT:
	case KMT_GETPREV:
		if (!altkv) {
			return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: validation");
		}
		/* no break here, all cmds need a kv ptr, so fall through */
		
	case KMT_GET:
	case KMT_GETVERS:
		if (!kv) {
			return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: validation");
		}
		break;

	default:
		return kstatus_err(K_EREJECTED, KI_ERR_INVARGS, "get: bad command");
	}
	
	/* Validate the passed in kv and if necessary the altkv */
	/* force=1 to ignore version field in the check */
	rc = ki_validate_kv(kv, (force=1), &ses->ks_l);
	if (rc < 0) {
		return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: invalid kv");
	}

	if (altkv) {
		/* force=1 to ignore version field in the check */
		rc = ki_validate_kv(altkv, (force=1), &ses->ks_l);
		if (rc < 0) {
			return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: invalid kv");
		}
	}
		
	/* create the kio structure; on failure, nothing malloc'd so we just return */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get: kio");
	}
	memset(kio, 0, sizeof(struct kio));
	kio->kio_cmd = msg_type;

	/* 
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See message.h for more details.
	 */
	kio->kio_sendmsg.km_cnt = 2; 
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get* request");
		goto gex_kio;
	}

	/* Hang the Packed PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len = KP_PLENGTH;

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
	 * kmreq. See below at gex_req:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type  = msg_type;

	kmreq = create_getkey_message(&msg_hdr, &cmd_hdr, kv);
	if (kmreq.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_CREATEREQ, "get: request");
		goto gex_req;
	}

	/* pack the message and hang it on the kio; this populates kio_sendmsg */
	/* PAK: Error handling */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	rc = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu, ppdu);
	printf("g_get_generic: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	ktli_send(ktd, kio);
	printf("Sent Kio: %p\n", kio);

	/* Wait for the response */
	do {
		/* wait for something to come in */
		ktli_poll(ktd, 0);

		/* Check to see if it our response; this populates kio_recvmsg */
		rc = ktli_receive(ktd, kio);
		if (rc < 0) {
			/* Not our response, so try again */
			if (errno == ENOENT) {
				continue;
			}
			else {
				/* PAK: need to exit, receive failed */
				krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVMSG, "getlog: recvmsg");
				goto gex_sendmsg;
			}
		}

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

	// unpack the message; KIOV_MSG may contain both msg and value
	kmresp = unpack_kinetic_message(kiov->kiov_base, rpdu.kp_msglen);
	if (kmresp.result_code == FAILURE) {
		krc = (kstatus_t) {
			.ks_code    = K_EINTERNAL,
			.ks_message = strdup("Error unpacking kinetic message"),
			.ks_detail  = strdup("get* request"),
		};

		goto gex_recvmsg;
	}

	/* 
	 * Grab the value and hang it on either the kv or altkv as approriate
	 * Also extract the kv data from response
	 * On extraction failure, both the response and request need to be cleaned up
	 */
	switch (msg_type) {
	case KMT_GET:
		kv->kv_val[0].kiov_base = kiov->kiov_base + rpdu.kp_msglen;
		kv->kv_val[0].kiov_len  = rpdu.kp_vallen;

		krc = extract_getkey(&kmresp, kv);
		if (krc.ks_code != K_OK) { goto gex_resp; }

		break;
		
	case KMT_GETNEXT:
	case KMT_GETPREV:
		altkv->kv_val[0].kiov_base = kiov->kiov_base + rpdu.kp_msglen;
		altkv->kv_val[0].kiov_len  = rpdu.kp_vallen;

		krc = extract_getkey(&kmresp, altkv);
		if (krc.ks_code != K_OK) { goto gex_resp; }

		break;

	case KMT_GETVERS:
		krc = extract_getkey(&kmresp, kv);
		if (krc.ks_code != K_OK) { goto gex_resp; }

		break;
		
	default:
		printf("get: should not get here\n");
		assert(0);
		break;
	}

#if 0
	/* PAK: Altkv may need to be validated as well */
	rc = ki_validate_kv(kv, (force=1), &ses->ks_l);
	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}

	if (krc.ks_message != NULL) { KI_FREE(krc.ks_message); }
	if (krc.ks_detail  != NULL) { KI_FREE(krc.ks_detail);  }
#endif

	
	/* clean up */
 gex_resp:
	destroy_message(kmresp.result_message);

 gex_recvmsg:

	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);

 gex_sendmsg:

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

 gex_req:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that 
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len = 0;

	destroy_message(kmreq.result_message);

 gex_kio:
	KI_FREE(kio);

	return (krc);
}

/**
 * ki_get(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_disum and kv_disumlen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Get the value specified by the given key. 
 *
 */
kstatus_t
ki_get(int ktd, kv_t *key)
{
	return(g_get_generic(ktd, key, NULL, KMT_GET));
}

/**
 * ki_getnext(int ktd, kv_t *kv, kv_t *next)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_disum and kv_disumlen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *  next	returned key value pair
 *		kv_key must contain a zero-ed kiovec array of cnt 1
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 *
 * Get the key value that follows the given key. 
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
 * 		kv_disum and kv_disumlen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *  prev	returned key value pair
 *		kv_key must contain a zero-ed kiovec array of cnt 1
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 *
 * Get the key value that is prior the given key. 
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
 * 		kv_disum and kv_disumlen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Get the version specified by the given key.  Do not return the value.
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
	int extract_result = keyname_to_proto(
		&(proto_cmd_body.key), cmd_data->kv_key, cmd_data->kv_keycnt
	);

	proto_cmd_body.has_key = extract_result;

	if (extract_result == 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = "Unable to copy key name to protobuf",
		};
	}

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(&proto_cmd_header, &proto_cmd_body);
	if (!command_bytes.data) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = "Unable to create or pack command data",
		};
	}

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
		if (!response_statusmsg) { return kv_status; }

		strcpy(response_statusmsg, response_status->statusmessage);

		char *response_detailmsg = NULL;
		if (response_status->has_detailedmessage) {
			response_detailmsg = (char *) KI_MALLOC(sizeof(char) * response_status->detailedmessage.len);
			if (!response_detailmsg) {
				KI_FREE(response_statusmsg);
				return kv_status;
			}

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

	// check that we received keyvalue information
	if (response_cmd->body->keyvalue == NULL) { return kv_status; }

	// ------------------------------
	// begin extraction of command body into kv_t structure
	kproto_kv_t *response = response_cmd->body->keyvalue;

	// we set the number of keys to 1, since this is not a range request
	kv_data->kv_keycnt = 1;

	// extract key name, db version, tag, and data integrity algorithm
	extract_bytes_optional(
		kv_data->kv_key->kiov_base, kv_data->kv_key->kiov_len,
		response, key
	);

	extract_bytes_optional(
		kv_data->kv_ver, kv_data->kv_verlen,
		response, dbversion
	);

	extract_bytes_optional(
		kv_data->kv_disum, kv_data->kv_disumlen,
		response, tag
	);

	extract_primitive_optional(kv_data->kv_ditype, response, algorithm);

	// set fields used for cleanup
	kv_data->kv_protobuf      = response_cmd;
	kv_data->destroy_protobuf = destroy_protobuf_getkey;

	return kv_status;
}
