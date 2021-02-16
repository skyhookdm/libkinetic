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

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"
#include "protocol_interface.h"

/**
 * Internal prototypes
 */
struct kresult_message
create_put_message(kmsghdr_t *, kcmdhdr_t *, kv_t *, int);

kstatus_t
p_put_generic(int ktd, kv_t *kv, kb_t *kb, int verck)
{
	int rc, i, n;
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
	struct kresult_message kmreq, kmresp, kmbat;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("put: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	/* Validate the passed in kv, if forcing a put do no verck */
	rc = ki_validate_kv(kv, verck, &ses->ks_l);
	if (rc < 0) {
		debug_printf("put: kv invalid");
		return(K_EINVAL);
	}

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		debug_printf("put: kio alloc");
		return(K_ENOMEM);
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
	 * kmreq. See below at pex2:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_PUT;

	/* sequence number gets set during the send */
	
	/* if necessary setup the batchid before creating the mesg */
	if (kb) {
		cmd_hdr.kch_bid = kb->kb_bid;
	}

	/* 
	 * Default put checks the version strings, if they don't match
	 * put fails.  Forcing the put avoids the version check. So if 
	 * checking the version, no forced put. If not, force it. 
	 */
	kmreq = create_put_message(&msg_hdr, &cmd_hdr, kv, (verck?0:1));
	if (kmreq.result_code == FAILURE) {
		debug_printf("put: request message create");
		krc = K_EINTERNAL;
		goto pex_kio;
	}

	/* Setup the KIO */
	kio->kio_cmd            = KMT_PUT;
	kio->kio_flags		= KIOF_INIT;
	if (kb)
		/* This is a batch put, there is no response */
		KIOF_SET(kio, KIOF_REQONLY);
	else
		/* This is a normal put, there is a response */
		KIOF_SET(kio, KIOF_REQRESP);

	/*
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message, and then elements 2 and beyond are
	 * for the value. The size is variable as the value can come in
	 * many parts from the caller. 
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL + kv->kv_valcnt;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		debug_printf("put: sendmesg alloc");
		krc = K_ENOMEM;
		goto pex_req;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

	/*
	 * copy the passed in value vector(s) onto the sendmsg,
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
		debug_printf("put: sendmesg msg pack");
		krc = K_EINTERNAL;
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

	/* Some batch accounting */
	if (kb) {
		pthread_mutex_lock(&kb->kb_m);
		
		kb->kb_ops++;

		/*
		 * PAK: this is an approximation, don't know what the 
		 * server actually implements for batch byte limits
		 */
		kb->kb_bytes += (pdu.kp_msglen + pdu.kp_vallen);

		pthread_mutex_unlock(&kb->kb_m);

		if (( ses->ks_l.kl_batlen > 0) &&
		    (kb->kb_bytes > ses->ks_l.kl_batlen)) {
			debug_printf("put: batch len");
			krc = K_EBATCH;
			goto pex_sendmsg;
		}
		if ((ses->ks_l.kl_batopscnt > 0) &&
		    (kb->kb_ops > ses->ks_l.kl_batopscnt)) {
			debug_printf("put: batch ops");
			krc = K_EBATCH;
			goto pex_sendmsg;

		}
	}

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
				krc = K_EINTERNAL;
				goto pex_sendmsg;
			}
		}

		else { break; }
	} while (1);

	/* Special case a batch put handling */
	if (kb) {
		/* 
		 * Batch receives only get the sendmsg KIO back, no resp.
		 * Therefore the rest of this put routine is not needed 
		 * except for the cleanup.
		 *
		 * Unpack the send message to get the final sequence
		 * number. The seq# represents an op in a batch. When the 
		 * batch is committed the server will return all of the 
		 * sequence numbers for each op in the batch. By preserving 
		 * each of the sent sequence numbers the batchend call can 
		 * validate all sequence numbers/ops are accounted for. 
		 */
		kiov = &kio->kio_sendmsg.km_msg[KIOV_MSG];
		kmbat = unpack_kinetic_message(kiov->kiov_base,
						kiov->kiov_len);
		if (kmbat.result_code == FAILURE) {
			debug_printf("put: sendmsg unpack");
			krc = K_EINTERNAL;
			goto pex_sendmsg;
		}

		krc = extract_cmdhdr(&kmbat, &cmd_hdr);
		if (krc == K_OK) {
			/* Preserve the req on the batch */
			b_batch_addop(kb, &cmd_hdr);
		}
		
		destroy_message(kmbat.result_message);
		
		/* normal exit, jump past all response handling */
		goto pex_sendmsg;
	}
	       
	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("put: PDU bad length");
		krc = K_EINTERNAL;
		goto pex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		debug_printf("put: PDU decode");
		krc = K_EINTERNAL;
		goto pex_recvmsg;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("put: msg unpack");
		krc = K_EINTERNAL;
		goto pex_recvmsg;
	}

	krc = extract_putkey(&kmresp, kv);

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
ki_put(int ktd, kbatch_t *kb, kv_t *kv)
{
	int verck;
	return(p_put_generic(ktd, kv, (kb_t *)kb, verck=0));
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
 * Once the version check is complete, the value specified by the given key,
 * data integrity value/type, and new version is put into the DB, using the
 * specified cache policy.
 */
kstatus_t
ki_cas(int ktd, kbatch_t *kb, kv_t *kv)
{
	int verck;
	return(p_put_generic(ktd, kv, (kb_t *)kb, verck=1));
}

/*
 * Helper functions
 */
struct kresult_message create_put_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
                                          kv_t *cmd_data, int bool_shouldforce) {
	kproto_kv_t proto_cmd_body;
	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

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

	// if newver or ver is set, propagate them (ver is passed as dbversion)
	if (cmd_data->kv_newver != NULL || cmd_data->kv_ver != NULL) {
		set_bytes_optional(&proto_cmd_body, newversion, cmd_data->kv_newver, cmd_data->kv_newverlen);
		set_bytes_optional(&proto_cmd_body, dbversion , cmd_data->kv_ver   , cmd_data->kv_verlen);
	}

	// if force is specified, then the dbversion is essentially ignored.
	set_primitive_optional(&proto_cmd_body, force          , bool_shouldforce    );
	set_primitive_optional(&proto_cmd_body, synchronization, cmd_data->kv_cpolicy);
	set_primitive_optional(&proto_cmd_body, algorithm      , cmd_data->kv_ditype );
	set_bytes_optional(&proto_cmd_body, tag, cmd_data->kv_disum, cmd_data->kv_disumlen);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(cmd_hdr, (void *) &proto_cmd_body);

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

void destroy_protobuf_putkey(kv_t *kv_data) {
	if (!kv_data) { return; }

	// destroy protobuf allocated memory
	destroy_command((kproto_kv_t *) kv_data->kv_protobuf);
}

kstatus_t extract_putkey(struct kresult_message *resp_msg, kv_t *kv_data) {
	// assume failure status
	kstatus_t krc = K_INVALID_SC;
	kproto_msg_t *kv_resp_msg;

	kv_resp_msg = (kproto_msg_t *) resp_msg->result_message;

	// check commandbytes exists
	if (!kv_resp_msg->has_commandbytes) {
		debug_printf("extract_putkey: no resp cmd");
		return(K_EINTERNAL);
	}

	// unpack command, and hang it on kv_data to be destroyed at any time
	kproto_cmd_t *resp_cmd;
	resp_cmd = unpack_kinetic_command(kv_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_putkey: resp cmd unpack");
		return(K_EINTERNAL);
	}
	kv_data->kv_protobuf = resp_cmd;

	// set destructor to be called later
	kv_data->destroy_protobuf = destroy_protobuf_putkey;

	// extract the status. On failure, skip to cleanup
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK) {
		debug_printf("extract_putkey: status");
		goto extract_pex;
	}

	// ------------------------------
	// begin extraction of command data

	// check if there's command data to parse, otherwise cleanup and exit
	if (!resp_cmd->body || !resp_cmd->body->keyvalue) {
		debug_printf("extract_putkey: command missing body or kv");
		goto extract_pex;
	}
	kproto_kv_t *resp = resp_cmd->body->keyvalue;

	// get the command data from the response
	// NOTE: this is tricky. Only modify the value if the response
	// returns a key (otherwise kv_key and kv_keycnt fall out of sync)
	if (resp->has_key) {
		kv_data->kv_keycnt = 1;
	}

	// extract key name, db version, tag, and data integrity algorithm	
	extract_bytes_optional(kv_data->kv_key->kiov_base,
			       kv_data->kv_key->kiov_len, resp, key);
    	extract_bytes_optional(kv_data->kv_ver,
			       kv_data->kv_verlen, resp, dbversion);
	extract_bytes_optional(kv_data->kv_disum,
			       kv_data->kv_disumlen, resp, tag);
	extract_primitive_optional(kv_data->kv_ditype, resp, algorithm);
	
	return krc;

 extract_pex:

	// Just make sure we don't return an ok message
	if (krc == K_OK) {
		debug_printf("extract_putkey: error exit");
		krc = K_EINTERNAL;
	}

	return krc;
}
