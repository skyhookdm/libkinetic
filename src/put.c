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
struct kresult_message
create_put_message(kmsghdr_t *, kcmdhdr_t *, kv_t *, int);

kstatus_t
p_put_generic(int ktd, kv_t *kv, int force)
{
	int rc, i;
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
	if (rc < 0) { return kstatus_err(K_EREJECTED, KI_ERR_BADSESS, "put: ktli config"); }

	ses = (ksession_t *) cf->kcfg_pconf;

	/* Validate the passed in kv */
	rc = ki_validate_kv(kv, force, &ses->ks_l);
	if (rc < 0) { return kstatus_err(errno, KI_ERR_INVARGS, "put: validation"); }

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) { return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "put: kio"); }

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
	 * kmreq. See below at pex2:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_PUT;

	kmreq = create_put_message(&msg_hdr, &cmd_hdr, kv, force);
	if (kmreq.result_code == FAILURE) {
		errno = K_EINTERNAL;
		krc   = kstatus_err(K_EINTERNAL, KI_ERR_CREATEREQ, "put: request");
		goto pex_kio;
	}

	/*
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message, and then elements 2 and beyond are
	 * for the value. The size is variable as the value can come in
	 * many parts from the caller. See message.h for more details.
	 */
	kio->kio_cmd            = KMT_PUT;
	kio->kio_sendmsg.km_cnt = 2 + kv->kv_valcnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(
		sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt
	);

	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "put: malloc sendmsg");
		goto pex_req;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

	/*
	 * copy the passed in value vector onto the sendmsg,
	 * no value data is copied
	 */
	memcpy(&(kio->kio_sendmsg.km_msg[KIOV_VAL]), kv->kv_val,
	       (sizeof(struct kiovec) * kv->kv_valcnt));

	/* pack the message and hang it on the kio */
	/* PAK: Error handling */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		errno = K_EINTERNAL;
		krc   = kstatus_err(errno, KI_ERR_MSGPACK, "put: pack msg");
		goto pex_sendmsg;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;

	/* for kp_vallen, need to run through kv_val vector and add it up */
	pdu.kp_vallen = 0;
	for (i = 0;i < kv->kv_valcnt; i++) {
		pdu.kp_vallen += kv->kv_val[i].kiov_len;
	}

	PACK_PDU(&pdu, ppdu);
	debug_printf("p_put_generic: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	ktli_send(ktd, kio);
	debug_printf("Sent Kio: %p\n", kio);

	// Wait for the response
	do {
		// wait for something to come in
		ktli_poll(ktd, 0);

		// check to see if it is our response
		rc = ktli_receive(ktd, kio);
		if (rc < 0) {
			// Not our response, so try again
			if (errno == ENOENT) { continue; }

			/* PAK: need to exit, receive failed */
			else {
				krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVMSG, "put: recvmsg");
				goto pex_sendmsg;
			}
		}

		else { break; }
	} while (1);

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVPDU, "put: extract PDU");
		goto pex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_PDUMSGLEN, "put: parse pdu");
		goto pex_recvmsg;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MSGUNPACK, "put: unpack msg");
		goto pex_recvmsg;
	}

	krc = extract_putkey(&kmresp, kv);

	// on failure, free anything that was allocated
	if (krc.ks_code != K_OK) { kv->destroy_protobuf(kv); }

	/* clean up */
	destroy_message(kmresp.result_message);

 pex_recvmsg:
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);

 pex_sendmsg:
	/* sendmsg.km_msg[KIOV_PDU] Not allocated, static */
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

 pex_req:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

 pex_kio:
	KI_FREE(kio);

	return (krc);
}

/**
 * ki_put(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain kiovec array representing the value
 * 		kv_vers and kv_verslen are optional
 * 		kv_dival and kv_divalen are optional.
 *		kv_ditype must be set if kv_dival is set
 *		kv_cpolicy sets the caching strategy for this put
 *			cpolicy of flush will flush the entire cache
 *
 * Put the value specified by the given key, data integrity value/type, and
 * new version, using the specified cache policy. This call will force the
 * put to the new values without any version checks.
 */
kstatus_t
ki_put(int ktd, kv_t *kv)
{
	int force;
	return(p_put_generic(ktd, kv, force=1));
}

/**
 * ki_cas(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain kiovec array representing the value
 * 		kv_vers and kv_verslen are required.
 * 		kv_newvers and kv_newverslen are required.
 * 		kv_disum and kv_disumlen are optional.
 *		kv_ditype must be set if kv_dival is set
 *		kv_cpolicy sets the caching strategy for this put
 *			cpolicy of flush will flush the entire cache
 *
 * CAS performs a compare and swap for the given key value. The key is only
 * put into the DB if kv_version matches the version in the DB. If there is
 * no match the operation fails with the error K_EVERSION.
 * Once the check is complete, the value specified by the given key,
 * data integrity value/type, and new version is put into the DB, using the
 * specified cache policy.
 */
kstatus_t
ki_cas(int ktd, kv_t *kv)
{
	int force;
	return(p_put_generic(ktd, kv, force=0));
}

/*
 * Helper functions
 */
struct kresult_message create_put_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
                                          kv_t *cmd_data, int bool_shouldforce) {

	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_kv_t     proto_cmd_body;

	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// extract from cmd_data into proto_cmd_body
	int extract_result = keyname_to_proto(
		&(proto_cmd_body.key), cmd_data->kv_key, cmd_data->kv_keycnt
	);
	proto_cmd_body.has_key = extract_result;

	if (extract_result == 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// if the keyval has a version set, then it is passed as newversion and we need to pass the old
	// version as dbversion
	if (cmd_data->kv_newver != NULL || cmd_data->kv_ver != NULL) {
		set_bytes_optional(&proto_cmd_body, newversion,
				   cmd_data->kv_newver, cmd_data->kv_newverlen);
		set_bytes_optional(&proto_cmd_body, dbversion,
				   cmd_data->kv_ver, cmd_data->kv_verlen);
	}

	// we could potentially compute disum here (based on integrity algorithm) if desirable
	set_primitive_optional(&proto_cmd_body, algorithm, cmd_data->kv_ditype);
	set_bytes_optional(&proto_cmd_body, tag,
			   cmd_data->kv_disum, cmd_data->kv_disumlen);

	// if force is specified, then the dbversion is essentially ignored.
	set_primitive_optional(&proto_cmd_body, force, bool_shouldforce);
	set_primitive_optional(&proto_cmd_body,
			       synchronization, cmd_data->kv_cpolicy);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, (void *) &proto_cmd_body
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	KI_FREE(proto_cmd_body.key.data);

	if (!command_bytes.data) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

// This may get a partially defined structure if we hit an error during the construction.
void destroy_protobuf_putkey(kv_t *kv_data) {
	// Don't do anything if we didn't get a valid pointer
	if (!kv_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_kv_t *) kv_data->kv_protobuf);
}

kstatus_t extract_putkey(struct kresult_message *response_msg, kv_t *kv_data) {
	// assume failure status
	kstatus_t kv_status = kstatus_err(K_INVALID_SC, KI_ERR_NOMSG, "");

	// commandbytes should exist, but we should probably be thorough
	kproto_msg_t *kv_response_msg = (kproto_msg_t *) response_msg->result_message;
	if (!kv_response_msg->has_commandbytes) { return kv_status; }

	// unpack the command bytes
	kproto_cmd_t *response_cmd = unpack_kinetic_command(kv_response_msg->commandbytes);
	if (!response_cmd) { return kv_status; }

	// extract the response status to be returned. prepare this early to make cleanup easy
	kproto_status_t *response_status = response_cmd->status;

	// copy the status message so that destroying the unpacked command doesn't get weird
	if (response_status && response_status->has_code) {
		// copy protobuf string
		size_t statusmsg_len     = strlen(response_status->statusmessage);
		char *response_statusmsg = (char *) KI_MALLOC(sizeof(char) * statusmsg_len);
		if (!response_statusmsg) {
			kv_status = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "extract put: status msg");
			goto extract_pex;
		}
		strcpy(response_statusmsg, response_status->statusmessage);

		// copy protobuf bytes field to null-terminated string
		char *response_detailmsg = NULL;
		copy_bytes_optional(response_detailmsg, response_status, detailedmessage);

		// assume malloc failed if there's a message but our pointer is still NULL
		if (response_status->has_detailedmessage && !response_detailmsg) {
			KI_FREE(response_statusmsg);
			kv_status = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "extract put: detail msg");

			goto extract_pex;
		}

		kv_status = (kstatus_t) {
			.ks_code    = response_status->code,
			.ks_message = response_statusmsg,
			.ks_detail  = response_detailmsg,
		};
	}

	// check if there's any keyvalue information to parse
	// (for PUT we expect this to be NULL or empty)
	if (!response_cmd->body || !response_cmd->body->keyvalue) { goto extract_pex; }

	// ------------------------------
	// begin extraction of command body into kv_t structure
	kproto_kv_t *response = response_cmd->body->keyvalue;

	// we set the number of keys to 1, since the key name is contiguous
	kv_data->kv_keycnt = response->has_key ? 1 : 0;

	// extract key name, db version, tag, and data integrity algorithm
	extract_bytes_optional(kv_data->kv_key->kiov_base,
			       kv_data->kv_key->kiov_len, response, key);

	extract_bytes_optional(kv_data->kv_ver,
			       kv_data->kv_verlen, response, dbversion);
	extract_bytes_optional(kv_data->kv_disum,
			       kv_data->kv_disumlen, response, tag);

	extract_primitive_optional(kv_data->kv_ditype, response, algorithm);

	// set fields used for cleanup
	kv_data->kv_protobuf      = response_cmd;
	kv_data->destroy_protobuf = destroy_protobuf_putkey;

	return kv_status;

 extract_pex:
	// set this so that the destroy function can correctly free it
	kv_data->kv_protobuf      = response_cmd;
	kv_data->destroy_protobuf = destroy_protobuf_putkey;

	return kv_status;
}
