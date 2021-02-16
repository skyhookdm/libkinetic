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
struct kresult_message create_batch_message(kmsghdr_t *, kcmdhdr_t *, uint32_t);
kstatus_t extract_status(struct kresult_message *resp_msg);

#ifdef KBATCH_SEQTRACKING
/**
 * List helper function to find a matching kio given a seq number
 * Return 0 for true or a match
 * Note: This function is only used when KBATCH_SEQTRACKING is defined
 */
static int
b_batch_seqmatch(char *data, char *ldata)
{
	kseq_t seq  = *(kseq_t *)data;
	kseq_t lseq = *(kseq_t *)ldata;

	// match
	return (seq == lseq) ? 0 : -1;
}
#endif

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
	int rc, retries, n;
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
		debug_printf("batch: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *)cf->kcfg_pconf;

	switch (msg_type) {
	case KMT_STARTBAT:
		/* Get a batch ID */
		/* arbitray large number to prevent an infinite loop */
		retries = 1000;
		bid     = ses->ks_bid;
		while (!SBCAS(&ses->ks_bid, bid, bid + 1) && retries--) {
			bid = ses->ks_bid;
		}

		if (!retries) {
			debug_printf("batch: no batch id");
			return(K_EBATCH);
		}

		/* Increment the number of active batches */
		/* arbitray large number to prevent an infinite loop */
		retries = 1000;
		batcnt = ses->ks_bats;
		while (!SBCAS(&ses->ks_bats, batcnt, batcnt + 1) && retries--)
				batcnt = ses->ks_bats;
		batcnt++;

		if (!retries) {
			debug_printf("batch: batch cnt increment");
			return(K_EBATCH);
		}

		if ((ses->ks_l.kl_devbatcnt > 0) &&
		    (batcnt > ses->ks_l.kl_devbatcnt)) {
			debug_printf("batch: too many batches");
			krc = K_EBATCH;
			goto bex_kb;
		}

		*kb = (kb_t *) KI_MALLOC(sizeof(kb_t));
		if (!(*kb)) {
			debug_printf("batch: kb alloc");
			krc = K_ENOMEM;
			goto bex_kb;
		}

		(*kb)->kb_ktd   = ktd;
		(*kb)->kb_bid   = bid;
		(*kb)->kb_seqs  = list_create();
		(*kb)->kb_ops   = 0;
		(*kb)->kb_dels  = 0;
		(*kb)->kb_bytes = 0;
		pthread_mutex_init(&((*kb)->kb_m), NULL);
		break;

	case KMT_ENDBAT:
		break;

	default:
		debug_printf("batch: bad command");
		return(K_EREJECTED);
	}

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		debug_printf("batch: kio alloc");
		krc = K_ENOMEM;
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
	cmd_hdr.kch_bid = (*kb)->kb_bid;

	kmreq = create_batch_message(&msg_hdr, &cmd_hdr, (*kb)->kb_ops);
	if (kmreq.result_code == FAILURE) {
		debug_printf("batch: request message create");
		krc = K_EINTERNAL;
		goto bex_kio;
	}

	/* Setup the KIO */
	kio->kio_cmd   = msg_type;
	kio->kio_flags = KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP); // Normal RPC

	/*
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		debug_printf("batch: sendmesg alloc");
		krc = K_ENOMEM;
		goto bex_req;
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
		debug_printf("batch: sendmesg msg pack");
		krc = K_EINTERNAL;
		goto bex_sendmsg;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	
	PACK_PDU(&pdu, ppdu);
	
	debug_printf("b_batch_generic: PDU(x%2x, %d, %d)\n",
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

			// PAK: need to exit, receive failed
			else {
				krc = K_EINTERNAL;
				goto bex_sendmsg;
			}
		}

		/* Got our response */
		break;
	} while (1);

	/* Parse response */

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("batch: PDU bad length");
		krc = K_EINTERNAL;
		goto bex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		debug_printf("batch: PDU decode");
		krc = K_EINTERNAL;
		goto bex_recvmsg;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov->kiov_base, rpdu.kp_msglen);
	if (kmresp.result_code == FAILURE) {
		debug_printf("batch: msg unpack");
		krc = K_EINTERNAL;
		goto bex_resp;
	}

	/* Handle the response based on msg_type */
	switch (msg_type) {
	case KMT_STARTBAT:
		krc = extract_status(&kmresp);
		break;

	case KMT_ENDBAT:
#ifndef KBATCH_SEQTRACKING
		krc = extract_status(&kmresp);
#else
		// these variables are only used if KBATCH_SEQTRACKING is defined
		size_t seqlistcnt;
		kseq_t *seqlist, *seq;

		krc = extract_seqlist(&kmresp, &seqlist, &seqlistcnt);
		if (krc != K_OK) {
			debug_printf("batch: seqlist extract");
			goto bex_endbat;
		}

		pthread_mutex_lock(&(*kb)->kb_m);

		debug_printf("Seq CNT: %lu\n", seqlistcnt);
		debug_printf("Seq List Size: %d\n", list_size((*kb)->kb_seqs));

		for (int i = 0; i < seqlistcnt; i++) {
			debug_printf("Searching for Seq: %lu", seqlist[i]);

			rc = list_traverse(
				(*kb)->kb_seqs, (char *)&seqlist[i],
				b_batch_seqmatch, LIST_ALTR
			);

			// Got an acknowledged op seq that is not
			// in our op seq list
			if (rc == LIST_EXTENT || rc == LIST_EMPTY) {
				debug_printf("NOT FOUND\n");
				krc = K_EBATCH;
				goto bex_endbat;
			}

			debug_printf("FOUND\n");
			seq = (kseq_t *)list_remove_curr((*kb)->kb_seqs);
			KI_FREE(seq);
		}

		// Did not get an acknowledged op seq for an op seq in our list
		if (list_size((*kb)->kb_seqs)) {
			debug_printf("batch: unacknowledged seq");
			krc = K_EBATCH;
		}
 // label for cleaning up batch data *in case KMT_ENDBAT*
 bex_endbat:
#endif /* KBATCH_SEQTRACKING */
		
		list_destroy((*kb)->kb_seqs, NULL);
		pthread_mutex_unlock(&(*kb)->kb_m);
		pthread_mutex_destroy(&(*kb)->kb_m);
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
	if ((krc != K_OK) || (msg_type == (kmtype_t) KMT_ENDBAT)) {
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
			return (K_EINTERNAL);
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
#ifdef KBATCH_SEQTRACKING
	pthread_mutex_lock(&kb->kb_m);
	(void)list_mvrear(kb->kb_seqs);
	if (!list_insert_after(kb->kb_seqs, &kc->kch_seq, sizeof(kseq_t))) {
		errno = ENOMEM;
		rc = -1;
	}
	pthread_mutex_unlock(&kb->kb_m);
#endif /* KBATCH_SEQTRACKING */
	return(rc);
}

/**
 * ki_batchstart(int ktd)
 */
kbatch_t *
i_batchstart(int ktd)
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
ki_submitbatch(int ktd, kbatch_t *kb)
{
	return (b_batch_generic(ktd, (kb_t **) &kb, KMT_ENDBAT));
}


/*
 * Helper functions
 */
struct kresult_message
create_batch_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, uint32_t ops) {
	// declare protobuf structs on stack
	kproto_batch_t  proto_cmd_body;
	com__seagate__kinetic__proto__command__batch__init(&proto_cmd_body);

	set_primitive_optional(&proto_cmd_body, count, ops);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(cmd_hdr, (void *) &proto_cmd_body);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

#ifdef KBATCH_SEQTRACKING

kstatus_t extract_seqlist(struct kresult_message *resp_msg, kseq_t **seqlist, size_t *seqlistcnt) {
	// assume failure status
	kstatus_t krc = K_INVALID_SC;
	kproto_msg_t *kb_resp_msg;

	// check that commandbytes exist, then unpack it
	kb_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!kb_resp_msg->has_commandbytes) {
		debug_printf("extract_seqlist: no resp cmd");
		return(K_EINTERNAL);
	}

	// TODO: memory leak that needs to be addressed:
	// (https://gitlab.com/kinetic-storage/kinetic-prototype/-/issues/16)
	kproto_cmd_t *resp_cmd;
	resp_cmd = unpack_kinetic_command(kb_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_seqlist: resp cmd unpack");
		return(K_EINTERNAL);
	}

	// extract the status from the command data
	krc = extract_cmdstatus_code(respcmd);
	if (krc != K_OK) {
		debug_printf("extract_seqlist: status");
		goto extract_dex;
	}
	
	// ------------------------------
	// begin extraction of command data
	// check if there's command data to parse, otherwise cleanup and exit
	if (!resp_cmd->body || !resp_cmd->body->batch) {
		debug_printf("extract_delkey: command missing body or kv");
		goto extract_emptybatch;
	}

	// begin extraction of command body into kv_t structure
	kproto_batch_t *resp = resp_cmd->body->batch;

	if (resp->has_failedsequence) {
		*seqlistcnt = 1;
		*seqlist    = &(resp>failedsequence);
	}
	else {
		*seqlistcnt = resp->n_sequence;
		*seqlist    = resp->sequence;
	}

	return krc;

 extract_emptybatch:

	destroy_command(response_cmd);

	return kb_status;
}
#endif /* KBATCH_SEQTRACKING */

kstatus_t extract_status(struct kresult_message *resp_msg) {
	// assume failure status
	kstatus_t krc = K_INVALID_SC;
	kproto_msg_t *kb_resp_msg;

	// check that commandbytes exist, then unpack it
	kb_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!kb_resp_msg->has_commandbytes) {
		debug_printf("extract_status: no resp cmd");
		return(K_EINTERNAL);
	}

	kproto_cmd_t *resp_cmd;
	resp_cmd = unpack_kinetic_command(kb_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_status: resp cmd unpack");
		return(K_EINTERNAL);
	}

	// extract the status from the command data
	krc = extract_cmdstatus_code(resp_cmd);

	destroy_command(resp_cmd);

	return krc;
}
