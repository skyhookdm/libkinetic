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
struct kresult_message create_batch_message(kmsghdr_t *, kcmdhdr_t *);
kstatus_t extract_batch(struct kresult_message *response_msg, kb_t *);
kstatus_t extract_status(struct kresult_message *response_msg);

/* 
 * KTLIBatch uses GCC builtin CAS for lockless atomic updates
 * bool __sync_bool_compare_and_swap (type *ptr, type oldval type newval, ...)
 *
 * atomic: if the curr value of *ptr == oldval, then write newval into *ptr. 
 * But does it have to be so long of a function name? Make it smaller
 */
#define SBCAS __sync_bool_compare_and_swap


kstatus_t
b_batch_generic(int ktd, kb_t **kb, kmtype_t msg_type)
{
	int rc, i, retries;
	kbid_t bid;
	uint32_t batcnt;
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
	
	switch (msg_type) {
	case KMT_STARTBAT:
		/* Get a batch ID */
		/* arbitray large number to prevent an infinite loop */
		retries = 1000; 
		bid = ses->ks_bid;
		while (!SBCAS(&ses->ks_bid, bid, bid + 1) && retries--)
		       bid = ses->ks_bid;

		if (!retries) {
			return (kstatus_t) {
				.ks_code = K_EINTERNAL,
				.ks_message = "Unable to get Batch ID",
				.ks_detail  = "",
			};
		}

		/* Increment the number of active batches */
		/* arbitray large number to prevent an infinite loop */
		retries = 1000;
		batcnt = ses->ks_bats;
		while (!SBCAS(&ses->ks_bats, batcnt, batcnt + 1) && retries--)
		       batcnt = ses->ks_bats;
		
		if (!retries) {
			return (kstatus_t) {
				.ks_code = K_EINTERNAL,
				.ks_message = "Unable to get Batch ID",
				.ks_detail  = "",
			};
		}
		
		if (batcnt > ses->ks_l.kl_devbatcnt) {
			krc = kstatus_err(K_EINTERNAL, KI_ERR_BATCH, "batch: Too many active");
			goto bex_kb;
		}
		
		*kb = (kb_t *) KI_MALLOC(sizeof(kb_t));
		if (!(*kb)) {
			krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "batch: kb");
			goto bex_kb;
		}

		(*kb)->kb_ktd   = ktd;
		(*kb)->kb_bid   = bid;
		(*kb)->kb_seqs  = list_init();
		(*kb)->kb_ops   = 0;
		(*kb)->kb_dels  = 0;
		(*kb)->kb_bytes = 0;
		pthread_mutex_init(&((*kb)->kb_m), NULL);
		break;
		
	case KMT_ENDBAT:
		break;

	default:
		return (kstatus_t) {
			.ks_code    = K_EREJECTED,
			.ks_message = "batch: bad command",
			.ks_detail  = "",
		};
	}
	
	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "batch: kio");
		goto bex_kb;
		
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
	cmd_hdr.kch_type = msg_type;

	kmreq = create_batch_message(&msg_hdr, &cmd_hdr);
	if (kmreq.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_CREATEREQ, "batch: request");
		goto bex_req;
	}

	/* Setup the KIO */
	kio->kio_cmd   = msg_type;
	kio->kio_flags = KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP); // Normal RPC

	/* 
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See message.h for more details.
	 */
	kio->kio_sendmsg.km_cnt = 2; 
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(
		sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt
	);

	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "batch: request");
		goto bex_kio;
	}

	/* Hang the Packed PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

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
		krc   = kstatus_err(errno, KI_ERR_MSGPACK, "batch: pack msg");
		goto bex_sendmsg;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu, ppdu);
	printf("b_batch_generic: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	ktli_send(ktd, kio);
	printf ("Sent Kio: %p\n", kio);

	// Wait for the response
	do {
		// wait for something to come in
		ktli_poll(ktd, 0);

		// check to see if it is our response
		rc = ktli_receive(ktd, kio);
		if (rc < 0) {
			// Not our response, so try again
			if (errno == ENOENT) { continue; }
			
			// PAK: need to exit, receive failed
			else {
				krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVMSG, "batch: recvmsg");
				goto bex_sendmsg;
			}
		}

		/* Got our response */
		break;
	} while (1);

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVPDU, "batch: extract PDU");
		goto bex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_PDUMSGLEN, "batch: parse pdu");
		goto bex_recvmsg;
	}

	/* Now unpack the message */ 
	kmresp = unpack_kinetic_message(kiov->kiov_base, rpdu.kp_msglen);
	if (kmresp.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MSGUNPACK, "batch: unpack msg");
		goto bex_recvmsg;
	}

	/* Handle the response based on msg_type */
	switch (msg_type) {
	case KMT_STARTBAT:
		krc = extract_status(&kmresp);
		break;
		
	case KMT_ENDBAT:
		krc = extract_batch(&kmresp, *kb);
		break;

	default:
		debug_printf("batch: should not get here\n");
		assert(0);
		break;
	}

 bex_resp:
	destroy_message(kmresp.result_message);

 bex_recvmsg:
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);

 bex_sendmsg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

 bex_req:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that 
	 * was made in kmreq before freeingcalling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len = 0;
	
	destroy_message(kmreq.result_message);

 bex_kio:
	KI_FREE(kio);

 bex_kb:
	if ((krc.ks_code != K_OK) || (msg_type == KMT_ENDBAT)) {
		/* 
		 * an error occurred or this is the exit of BATCHEND
		 * either way get rid of the batch and decrement the active 
		 * batches.
		 */
		if (*kb) {
			KI_FREE(*kb);
			*kb = NULL;
		}
		
		/* Decrement the number of active batches */
		/* arbitray large number to prevent an infinite loop */
		retries = 1000;
		batcnt  = ses->ks_bats;

		while (!SBCAS(&ses->ks_bats, batcnt, batcnt - 1) && retries--) {
			batcnt = ses->ks_bats;
		}

		if (!retries) {
			return (kstatus_t) {
				.ks_code    = K_EINTERNAL,
				.ks_message = "Unable to release batch",
				.ks_detail  = "",
			};
		}
		/* fall though using the krc from the op */
	}
	
	return (krc);
}

int
b_batch_addop(kb_t *kb, kcmdhdr_t *kc)
{
	int rc = 0;
	if (!kb || !kc) return (-1);

	pthread_mutex_lock(&kb->kb_m);
	(void)list_mvrear(kb->kb_seqs);
	if (!list_insert_after(kb->kb_seqs, &kc->kch_seq, sizeof(kseq_t))) {
		errno = ENOMEM;
		rc = -1;
	}
	pthread_mutex_unlock(&kb->kb_m);
	return(rc);
}

/**
 * ki_batchcreate(int ktd)
 */
kbatch_t *
ki_batchcreate(int ktd)
{
	kb_t *kb;

	b_batch_generic(ktd, &kb, KMT_STARTBAT);
	return ((kbatch_t *)kb);
}

/**
 * ki_batchend(int ktd, kbatch_t *kb)
 *
 */
kstatus_t
ki_batchend(int ktd, kbatch_t *kb)
{
	return (b_batch_generic(ktd, (kb_t **) &kb, KMT_ENDBAT));
}

kstatus_t
ki_batchput(int ktd, kv_t *kv)
{
	int force;
	return (p_put_generic(ktd, kv, force=1));
}
kstatus_t
ki_batchdel(int ktd, kv_t *key)
{
	int force;
	return (p_put_generic(ktd, kv, force=1));
}

/*
 * Helper functions
 */
struct kresult_message
create_batch_message(kmsghdr_t *msg_hdr,
		     kcmdhdr_t *cmd_hdr) {

#if 0
	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_batch_t  proto_cmd_body;

	com__seagate__kinetic__proto__command__batch__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, (void *) &proto_cmd_body
	);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
#endif
}


kstatus_t extract_status(struct kresult_message *response_msg)
{
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

	kproto_status_t *response_status = response_cmd->status;
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
	return(kv_status);
}

// This may get a partially defined structure if we hit an error during the construction.
void destroy_protobuf_batch(kb_t *kb_data) {
	// Don't do anything if we didn't get a valid pointer
	if (!kb_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_batch_t *) kb_data->kb_protobuf);
}

kstatus_t extract_batch(struct kresult_message *response_msg, kb_t *kb_data) {
	// assume failure status
	kstatus_t kb_status = kstatus_err(K_INVALID_SC, KI_ERR_NOMSG, "");

	// commandbytes should exist, but we should probably be thorough
	kproto_msg_t *kb_response_msg = (kproto_msg_t *) response_msg->result_message;
	if (!kb_response_msg->has_commandbytes) { return kb_status; }

	// unpack the command bytes
	kproto_cmd_t *response_cmd = unpack_kinetic_command(kb_response_msg->commandbytes);
	if (!response_cmd) { return kb_status; }

	// extract the response status to be returned. prepare this early to make cleanup easy
	kproto_status_t *response_status = response_cmd->status;

	// copy the status message so that destroying the unpacked command doesn't get weird
	if (response_status && response_status->has_code) {
		// copy protobuf string
		size_t statusmsg_len     = strlen(response_status->statusmessage);
		char *response_statusmsg = (char *) KI_MALLOC(sizeof(char) * statusmsg_len);
		if (!response_statusmsg) {
			kb_status = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "extract batch: status msg");
			goto extract_bex;
		}
		strcpy(response_statusmsg, response_status->statusmessage);

		// copy protobuf bytes field to null-terminated string
		char *response_detailmsg = NULL;
		copy_bytes_optional(response_detailmsg, response_status, detailedmessage);

		// assume malloc failed if there's a message but our pointer is still NULL
		if (response_status->has_detailedmessage && !response_detailmsg) {
			KI_FREE(response_statusmsg);
			kb_status = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "extract put: detail msg");

			goto extract_bex;
		}

		kb_status = (kstatus_t) {
			.ks_code    = response_status->code,
			.ks_message = response_statusmsg,
			.ks_detail  = response_detailmsg,
		};
	}

	// check if there's any keyvalue information to parse
	// (for PUT we expect this to be NULL or empty)
	if (!response_cmd->body || !response_cmd->body->batch) { return kb_status; }

	// ------------------------------
	// begin extraction of command body into kv_t structure
	kproto_batch_t *response = response_cmd->body->batch;

	if (response->has_failedsequence) {
		kb_data->seqlistcnt = 1;
		kb_data->seqlist    = &(response->failedsequence);
	}
	else {
		kb_data->seqlistcnt = response->n_sequence;
		kb_data->seqlist    = response->sequence;
	}

 extract_bex:

	// set fields used for cleanup
	kv_data->kb_protobuf      = response_cmd;
	kv_data->destroy_protobuf = destroy_protobuf_batch;

	return kb_status;
}
