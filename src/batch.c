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
/** KBATCH_SEQTRACKING IS NOT WORKING CODE DO NOT ENABLE **/
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
#endif /* KBATCH_SEQTRACKING */


/*
 * Batch uses GCC builtin CAS for lockless atomic updates
 * bool __sync_bool_compare_and_swap (type *ptr, type oldval type newval, ...)
 *
 * atomic: if the curr value of *ptr == oldval, then write newval into *ptr.
 *
 * Increment the counter in p by i (i can be neg for a decrement)
 * Return the new value in v
 */
int
b_atom_inc(uint32_t *p, uint32_t *v, int32_t i)
{
	/* Retries set to arbitray large num to prevent an infinite loop. */
	int retries = 1000;

	*v = *p;
	while (!__sync_bool_compare_and_swap(p, *v, *v + i) && retries--)
		*v = *p;

	if (!retries)
		return(-1);

	return(0);
}

kstatus_t
b_batch_aio_generic(int ktd, kb_t *kb, kmtype_t msg_type,
		    void *cctx, kio_t **ckio)
{
	int rc, n;			/* return code, temps */
	kstatus_t krc;			/* Kinetic return code */
	uint32_t batcnt;		/* Temp used to inc/dec batch counts */
	struct kio *kio;		/* Built and returned KIO */
	ksession_t *ses;		/* KTLI Session info */
	kmsghdr_t msg_hdr;		/* Unpacked message header */
	kcmdhdr_t cmd_hdr;		/* Unpacked Command header */
	struct ktli_config *cf;		/* KTLI configuration info */
	struct kresult_message kmreq;	/* Intermediate resp representation */
	kpdu_t pdu;			/* Unpacked PDU structure */

	if (!ckio) {
		debug_printf("batch: kio ptr required\n");
		return(K_EINVAL);
	}

	/* Clear the callers kio, ckio */
	*ckio = NULL;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("batch: ktli config\n");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	/* Validate the passed in kb, if any */
	rc =  ki_validate_kb(kb, msg_type);
	if (kb && (rc < 0)) {
		debug_printf("batch: kb invalid\n");
		return(K_EINVAL);
	}

	switch (msg_type) {
	case KMT_STARTBAT:
		/*
		 * Setup the batch. Start by atomically incrementing the
		 * session batch ID and setting it on the batch. 
		 */
		if (b_atom_inc(&ses->ks_bid, &kb->kb_bid, 1) < 0) {
			debug_printf("batch: no batch id\n");
			return(K_EBATCH);
		}

		/*
		 * Now atomically increment the number of active batches
		 * on the sessions.
		 */
		if (b_atom_inc(&ses->ks_bats, &batcnt, 1) < 0) {
			debug_printf("batch: batch cnt increment\n");
			return(K_EBATCH);
		}

		if ((ses->ks_l.kl_devbatcnt > 0) &&
		    (batcnt > ses->ks_l.kl_devbatcnt)) {
			debug_printf("batch: too many batches\n");
			krc = K_EBATCH;
			goto bex_kb;
		}

		/* setup the rest of the batch */
		kb->kb_ktd   = ktd;
		kb->kb_ops   = 0;
		kb->kb_dels  = 0;
		kb->kb_bytes = 0;
		pthread_mutex_init(&kb->kb_m, NULL);

#ifdef KBATCH_SEQTRACKING
		kb->kb_seqs  = list_create();
#endif
		break;

	case KMT_ENDBAT:
	case KMT_ABORTBAT:
		/* Nothing to do here, just validating the msg_type */
		break;

	default:
		debug_printf("batch: bad command\n");
		return(K_EREJECTED);
	}

	/*
	 * create the kio structure; on failure,
	 * nothing malloc'd so we just return
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		debug_printf("batch: kio alloc\n");
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
	 * be used later on to calculate the actual HMAC which will then
	 * replace the HMAC key on the kmh_hmac field.
	 *
	 * A reference is made to the kcfg_hkey ptr in the kmreq. This
	 * reference needs to be removed before destroying kmreq.  The
	 * protobuf code will try to clean up this ptr which is an outside
	 * unfreeable ptr.  See below at bex_kmreq:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KAT_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = msg_type;

	/* Set the command batchid before creating the mesg */
	cmd_hdr.kch_bid = kb->kb_bid;

	kmreq = create_batch_message(&msg_hdr, &cmd_hdr, kb->kb_ops);
	if (kmreq.result_code == FAILURE) {
		debug_printf("batch: request message create\n");
		krc = K_EINTERNAL;
		goto bex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= msg_type;
	kio->kio_flags	= KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP); // Normal RPC

	kio->kio_ckb	= kb;		/* Hang the callers kb, if any */
	kio->kio_cctx	= cctx;		/* Hang the callers context */

	/*
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);

	if (!kio->kio_sendmsg.km_msg) {
		debug_printf("batch: sendmesg alloc\n");
		krc = K_ENOMEM;
		goto bex_kmreq;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);

	if (!kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base) {
		debug_printf("batch: sendmesg PDU alloc\n");
		krc = K_ENOMEM;
		goto bex_kmmsg;
	}

	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("batch: sendmesg msg pack\n");
		krc = K_EINTERNAL;
		goto bex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu,  (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("batch: PDU(x%2x, %d, %d)\n",
		     pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		debug_printf("batch: kio send\n");
		krc = K_EINTERNAL;
		goto bex_kmmsg_msg;
	}
	debug_printf("Sent Kio: %p\n", kio);

	/*
	 * Successful Exit.
	 * Return the kio.
	 * Cleanup before return, the only thing that needs to go
	 * is the unpacked protobuf request message kmreq.
	 *
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	*ckio = kio;

	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len = 0;

	destroy_message(kmreq.result_message);

	return(K_OK);

	/* Error Exit. */

	/* bex_kmmsg_val:
	 * Nothing to do as batch are just messages no values
	 */

 bex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 bex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);

 bex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 bex_kmreq:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

 bex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

 bex_kb:
	/* if start batch fails we need to do some cleanup */
	if (msg_type == (kmtype_t) KMT_STARTBAT) {
		/* decrement the active count */
		if (b_atom_inc(&ses->ks_bats, &batcnt, -1) < 0) {
			debug_printf("batch: batch cnt start decrement\n");
			return(K_EBATCH);
		}

		/* destroy the kb mutex */
		pthread_mutex_destroy(&kb->kb_m);
	}

	return (krc);
}


/*
 * Complete a AIO start, end, abort batch  call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated.
 */
kstatus_t
b_batch_aio_complete(int ktd, struct kio *kio, void **cctx)
{
	int rc, i;			/* return code, temps */
	uint32_t batcnt;		/* Temp batch count */
	kb_t *kb;			/* Set to KB passed in orig aio call */
	kpdu_t pdu;			/* Unpacked PDU Structure */
	kstatus_t krc;			/* Returned status */
	ksession_t *ses;		/* KTLI Session info */
	struct kiovec *kiov;		/* Message KIO vector */
	struct ktli_config *cf;		/* KTLI configuration info */
	struct kresult_message kmresp;	/* Intermediate resp representation */

	/* Setup in case of an error return */
	if (cctx)
		*cctx = NULL;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("batch: complete ktli config\n");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	if (!kio  || (kio && (kio->kio_magic !=  KIO_MAGIC))) {
		debug_printf("batch: kio invalid\n");
		return(K_EINVAL);
	}

	rc = ktli_receive(ktd, kio);
	if (rc < 0) {
		if (errno == ENOENT) {
			/* No available response, so try again */
			debug_printf("batch: kio not available\n");
			return(K_EAGAIN);
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			debug_printf("batch: kio receive\n");
			return(K_EINTERNAL);
		}
	}

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go.
	 */
	if (kio->kio_state != KIO_RECEIVED) {
		debug_printf("batch: kio bad state\n");
		krc = K_EINTERNAL;
		goto bex;
	}

	/* Got a RECEIVED KIO, validate and decode */

	/*
	 * Grab the original KB sent in from the caller.
	 * Although these are not directly passed back in the complete,
	 * the caller should have maintained them across the originating
	 * aio call and the complete.
	 */
	kb = kio->kio_ckb;

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("batch: PDU bad length\n");
		krc = K_EINTERNAL;
		goto bex;
	}
	UNPACK_PDU(&pdu, ((uint8_t *)(kiov->kiov_base)));

	/*
	 * Does the PDU match what was given in the recvmsg
	 * Value is always there even if len = 0
	 */
	kiov = kio->kio_recvmsg.km_msg;
	if ((pdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (pdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		debug_printf("batch: PDU decode\n");
		krc = K_EINTERNAL;
		goto bex;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base,
					kiov[KIOV_MSG].kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("batch: msg unpack\n");
		krc = K_EINTERNAL;
		goto bex;
	}

	krc = extract_status(&kmresp);

#ifdef KBATCH_SEQTRACKING
	/* Handle the end batch accounting */
	if ((krc == K_OK) && (kio->kio_cmd == KMT_ENDBAT)) {
		// these vars are only used if KBATCH_SEQTRACKING is defined
		size_t seqlistcnt;
		kseq_t *seqlist, *seq;

		krc = extract_seqlist(&kmresp, &seqlist, &seqlistcnt);
		if (krc != K_OK) {
			debug_printf("batch: seqlist extract\n");
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
				pthread_mutex_unlock(&(*kb)->kb_m);
				goto bex_endbat;
			}

			debug_printf("FOUND\n");
			seq = (kseq_t *)list_remove_curr((*kb)->kb_seqs);
			KI_FREE(seq);
		}

		// Did not get an acknowledged op seq for an op seq in our list
		if (list_size((*kb)->kb_seqs)) {
			debug_printf("batch: unacknowledged seq\n");
			krc = K_EBATCH;
		}
	}

 // label for cleaning up batch data *in case KMT_ENDBAT*
 bex_endbat:
	list_destroy(kb->kb_seqs, NULL);

#endif /* KBATCH_SEQTRACKING */

	/* if Successful end or abort batch, need to do some cleanup */
	if ((krc == K_OK) &&
	    ((kio->kio_cmd == (kmtype_t) KMT_ENDBAT) ||
	     (kio->kio_cmd == (kmtype_t) KMT_ABORTBAT))) {
		/* decrement the active count */
		if (b_atom_inc(&ses->ks_bats, &batcnt, -1) < 0) {
			debug_printf("batch: batch cnt end decrement\n");
			return(K_EBATCH);
		}

		/* destroy the kb mutex */
		pthread_mutex_destroy(&kb->kb_m);
	}


	/* clean up */
	destroy_message(kmresp.result_message);

 bex:
	/* depending on errors the recvmsg may or may not exist */
	if (kio->kio_recvmsg.km_msg) {
		for (i=0; i < kio->kio_recvmsg.km_cnt; i++) {
			KI_FREE(kio->kio_recvmsg.km_msg[i].kiov_base);
		}
		KI_FREE(kio->kio_recvmsg.km_msg);
	}

	/* sendmsg always exists here but doesn't have a PDU_VAL */
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

	memset(kio, 0, sizeof(struct kio));
	KI_FREE(kio);

	return (krc);
}


kstatus_t
b_batch_generic(int ktd, kb_t *kb, kmtype_t msg_type)
{
	kstatus_t ks;
	kio_t *kio;

	ks = b_batch_aio_generic(ktd, kb, msg_type, NULL, &kio);
	if (ks != K_OK) {
		return(ks);
	}

	/* Wait for a response */
	do {
		if (ktli_poll(ktd, 100) < 1)  {
			/* Poll timed out, poll again */
			if (errno == ETIMEDOUT)
				continue;
		}

		/*
		 * Poll either succeeded or failed, either way call
		 * complete. In the case of error, the complete will
		 * try to retrieve the failed KIO
		 */
		ks = b_batch_aio_complete(ktd, kio, NULL);
		if (ks == K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;

	} while (1);

	return(ks);
}


int
b_batch_addop(kb_t *kb, kcmdhdr_t *kc)
{
	int rc = 0;
#ifdef KBATCH_SEQTRACKING
	if (!kb || !kc) return (-1);
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
kstatus_t
b_startbatch(int ktd, kbatch_t *kb)
{
	return (b_batch_generic(ktd, (kb_t *)kb, KMT_STARTBAT));
}


/**
 * ki_aio_submitbatch(int ktd, kbatch_t *kb, void *cctx, kio_t **kio)
 *
 */
kstatus_t
ki_aio_submitbatch(int ktd, kbatch_t *kb, void *cctx, kio_t **kio)
{
	return (b_batch_aio_generic(ktd, (kb_t *)kb, KMT_ENDBAT, cctx, kio));
}


/**
 * ki_submitbatch(int ktd, kbatch_t *kb)
 *
 */
kstatus_t
ki_submitbatch(int ktd, kbatch_t *kb)
{
	return (b_batch_generic(ktd, (kb_t *)kb, KMT_ENDBAT));
}


/**
 * ki_aio_abortbatch(int ktd, kbatch_t *kb, void *cctx, kio_t **kio)
 *
 */
kstatus_t
ki_aio_abortbatch(int ktd, kbatch_t *kb,void *cctx, kio_t **kio)
{
	return (b_batch_aio_generic(ktd, (kb_t *)kb, KMT_ABORTBAT, cctx, kio));
}


/**
 * ki_abortbatch(int ktd, kbatch_t *kb)
 *
 */
kstatus_t
ki_abortbatch(int ktd, kbatch_t *kb)
{
	return (b_batch_generic(ktd, (kb_t *)kb, KMT_ABORTBAT));
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
		debug_printf("extract_seqlist: no resp cmd\n");
		return(K_EINTERNAL);
	}

	// TODO: memory leak that needs to be addressed:
	// (https://gitlab.com/kinetic-storage/libkinetic/-/issues/16)
	kproto_cmd_t *resp_cmd;
	resp_cmd = unpack_kinetic_command(kb_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_seqlist: resp cmd unpack\n");
		return(K_EINTERNAL);
	}

	// extract the status from the command data
	krc = extract_cmdstatus_code(respcmd);
	if (krc != K_OK) {
		debug_printf("extract_seqlist: status\n");
		goto extract_dex;
	}
	
	// ------------------------------
	// begin extraction of command data
	// check if there's command data to parse, otherwise cleanup and exit
	if (!resp_cmd->body || !resp_cmd->body->batch) {
		debug_printf("extract_delkey: command missing body or kv\n");
		goto extract_emptybatch;
	}

	// begin extraction of command body into kv_t structure
    // TODO: this has not been updated to do proper memory management in the ktb style.
	kproto_batch_t *resp = resp_cmd->body->batch;

	if (resp->has_failedsequence) {
		*seqlistcnt = 1;
		*seqlist    = &(resp->failedsequence);
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
		debug_printf("extract_status: no resp cmd\n");
		return(K_EINTERNAL);
	}

	kproto_cmd_t *resp_cmd;
	resp_cmd = unpack_kinetic_command(kb_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_status: resp cmd unpack\n");
		return(K_EINTERNAL);
	}

	// extract the status from the command data
	krc = extract_cmdstatus_code(resp_cmd);

	destroy_command(resp_cmd);

	return krc;
}
