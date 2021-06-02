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
create_delkey_message(kmsghdr_t *, kcmdhdr_t *, kv_t *, int);


kstatus_t
d_del_aio_generic(int ktd, kv_t *kv, kb_t *kb, int verck,
		  void *cctx, kio_t **ckio)
{
	int rc, n;			/* return code, temps */
	kstatus_t krc;			/* Kinetic return code */
	struct kio *kio;		/* Built and returned KIO */
	ksession_t *ses;		/* KTLI Session info */
	kstats_t *kst;			/* Kinetic Stats */
	kmsghdr_t msg_hdr;		/* Unpacked message header */
	kcmdhdr_t cmd_hdr;		/* Unpacked Command header */
	struct ktli_config *cf;		/* KTLI configuration info */
	struct kresult_message kmreq;	/* Intermediate resp representation */
	kpdu_t pdu;			/* Unpacked PDU structure */

	struct timespec	start;		/* Temp start timestamp */

	/*
	 * Sending a op, record the clock. The session is not known yet.
	 * This is the begining of the send and the natural spot to
	 * start the clock on send processing time. Without knowing
	 * the session info it is not known if the code should be recording
	 * timestamps. So this maybe wasted code. However vdso(7) makes
	 * this fast, Not going to worry about this.
	 */
	ktli_gettime(&start);

	if (!ckio) {
		debug_printf("del: kio ptr required");
		return(K_EINVAL);
	}

	/* Clear the callers kio, ckio */
	*ckio = NULL;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("del: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	kst = &ses->ks_stats;

	/* Validate the passed in kv, if forcing a del do no verck  */
	rc = ki_validate_kv(kv, verck, &ses->ks_l);
	if (rc < 0) {
		kst->kst_dels.kop_err++;
		debug_printf("del: kv invalid");
		return(K_EINVAL);
	}

	/* Validate the passed in kb, if any */
	rc =  ki_validate_kb(kb, KMT_PUT);
	if (kb && (rc < 0)) {
		kst->kst_dels.kop_err++;
		debug_printf("put: kb invalid");
		return(K_EINVAL);
	}

	/*
	 * create the kio structure; on failure,
	 * nothing malloc'd so we just return
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		kst->kst_dels.kop_err++;
		debug_printf("del: kio alloc");
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
	 * be used later on to calculate the actual HMAC which will then
	 * replace the HMAC key on the kmh_hmac field.
	 *
	 * A reference is made to the kcfg_hkey ptr in the kmreq. This
	 * reference needs to be removed before destroying kmreq.  The
	 * protobuf code will try to clean up this ptr which is an outside
	 * unfreeable ptr.  See below at dex_kmreq:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_DEL;

	/* if necessary setup the batchid before creating the mesg */
	if (kb) {
		cmd_hdr.kch_bid = kb->kb_bid;
	}

	/* 
	 * Default del checks the version strings, if they don't match
	 * del fails.  Forcing the del avoids the version check. So if 
	 * checking the version, no forced del.
	 */
	kmreq = create_delkey_message(&msg_hdr, &cmd_hdr, kv, (verck?0:1));
	if (kmreq.result_code == FAILURE) {
		debug_printf("del: request message create");
		krc = K_EINTERNAL;
		goto dex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= KMT_DEL;
	kio->kio_flags	= KIOF_INIT;

	if (kb)
		/* This is a batch del, there is no response */
		KIOF_SET(kio, KIOF_REQONLY);
	else
		/* This is a normal del, there is a response */
		KIOF_SET(kio, KIOF_REQRESP);

	/* If timestamp tracking is enabled for this op set it in the KIO */
	if (KIOP_ISSET((&kst->kst_dels), KOPF_TSTAT)) {
		/* Deal with time stamps */
		KIOF_SET(kio, KIOF_TSTAMP);
		kio->kio_ts.kiot_start = start;
	}

	kio->kio_ckv	= kv;		/* Hang the callers kv */
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
		debug_printf("del: sendmesg alloc");
		krc = K_ENOMEM;
		goto dex_kmreq;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);

	if (!kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base) {
		debug_printf("del: sendmesg PDU alloc");
		krc = K_ENOMEM;
		goto dex_kmmsg;
	}

	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("del: sendmesg msg pack");
		krc = K_EINTERNAL;
		goto dex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu,  (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("del: PDU(x%2x, %d, %d)\n",
		     pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	/* Some batch accounting */
	if (kb) {
		pthread_mutex_lock(&kb->kb_m);
		
		kb->kb_ops++;
		kb->kb_dels++;
		
		/*
		 * PAK: this is an approximation, don't know what the 
		 * server actually implements for batch byte limits
		 */
		kb->kb_bytes += (pdu.kp_msglen + pdu.kp_vallen);

		pthread_mutex_unlock(&kb->kb_m);

		if (( ses->ks_l.kl_batlen > 0) &&
		    (kb->kb_bytes > ses->ks_l.kl_batlen)) {
			debug_printf("del: batch len");
			krc = K_EBATCH;
			goto dex_kmmsg_msg;
		}
		if ((ses->ks_l.kl_batopscnt > 0) &&
		    (kb->kb_ops > ses->ks_l.kl_batopscnt)) {
			debug_printf("del: batch ops");
			krc = K_EBATCH;
			goto dex_kmmsg_msg;

		}
		if ((ses->ks_l.kl_batdelcnt > 0 ) &&
		    (kb->kb_dels > ses->ks_l.kl_batdelcnt)) {
			debug_printf("del: batch del ops");
			krc = K_EBATCH;
			goto dex_kmmsg_msg;
		}
	}

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		debug_printf("del: kio send");
		krc = K_EINTERNAL;
		goto dex_kmmsg_msg;
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

	/* dex_kmmsg_val:
	 * Nothing to do as del KVs are just keys no values
	 */

 dex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 dex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);

 dex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 dex_kmreq:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

 dex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

	kst->kst_dels.kop_err++; /* Record the error in the stats */

	return (krc);
}


/*
 * Complete a AIO del/cad call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated.
 */
kstatus_t
d_del_aio_complete(int ktd, struct kio *kio, void **cctx)
{
	int rc, i;
	uint32_t sl=0, rl=0, kl=0, vl=0;/* Stats send/recv/key/val lengths */
	kv_t *kv;			/* Set to KV passed in orig aio call */
	kb_t *kb;			/* Set to KB passed in orig aio call */
	kpdu_t pdu;			/* Unpacked PDU Structure */
	ksession_t *ses;		/* KTLI Session info */
	kstats_t *kst;			/* Kinetic Stats */
	kstatus_t krc;			/* Returned status */
	struct ktli_config *cf;		/* KTLI configuration info */
	struct kiovec *kiov;		/* Message KIO vector */
	struct kresult_message kmresp;	/* Intermediate resp representation */

	/* Setup in case of an error return */
	if (cctx)
		*cctx = NULL; 

	if (!kio  || (kio && (kio->kio_magic !=  KIO_MAGIC))) {
		debug_printf("del: kio invalid");
		return(K_EINVAL);
	}

	/* Get KTLI config, Kinetic session and Kinetic stats structure */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("put: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	kst = &ses->ks_stats;

	rc = ktli_receive(ktd, kio);
	if (rc < 0) {
		if (errno == ENOENT) {
			/* No available response, so try again */
			debug_printf("del: kio not available");
			return(K_EAGAIN);
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			kst->kst_dels.kop_err++;
			debug_printf("del: kio receive failed");
			return(K_EINTERNAL);
		}
	}

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go.
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("del: kio timed out");
		kst->kst_dels.kop_err++;
		krc = K_ETIMEDOUT;
		goto dex;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("del: kio failed");
		kst->kst_dels.kop_err++;
		krc = K_ENOMSG;
		goto dex;
	}

	/* Got a RECEIVED KIO, validate and decode */

	/*
	 * Grab the original KV and KB sent in from the caller.
	 * Although these are not directly passed back in the complete,
	 * the caller should have maintained them across the originating
	 * aio call and the complete.
	 */
	kv = kio->kio_ckv;
	kb = kio->kio_ckb;

	/* Special case a batch del handling */
	if (kb) {
		krc = K_OK;

#ifdef KBATCH_SEQTRACKING
		/* 
		 * Batch receives only the sendmsg KIO back, no resp.
		 * Therefore the rest of this del routine is not needed 
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
			debug_printf("del: sendmsg unpack");
			krc = K_EINTERNAL;
			goto dex;
		}

		krc = extract_cmdhdr(&kmbat, &cmd_hdr);
		if (krc == K_OK) {
			/* Preserve the req on the batch */
			b_batch_addop(kb, &cmd_hdr);
		}
		
		destroy_message(kmbat.result_message);
#endif /* KBATCH_SEQTRACKING */

		/* normal exit, jump past all response handling */
		goto dex;
	}

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("del: PDU bad length");
		krc = K_EINTERNAL;
		goto dex;
	}
	UNPACK_PDU(&pdu, ((uint8_t *)(kiov->kiov_base)));

	/*
	 * Does the PDU match what was given in the recvmsg
	 * Value is always there even if len = 0
	 */
	kiov = kio->kio_recvmsg.km_msg;
	if ((pdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (pdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		debug_printf("del: PDU decode");
		krc = K_EINTERNAL;
		goto dex;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base,
					kiov[KIOV_MSG].kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("del: msg unpack");
		krc = K_EINTERNAL;
		goto dex;
	}

	krc = extract_delkey(&kmresp, kv);

	/* clean up */
	destroy_message(kmresp.result_message);

 dex:
	/* depending on errors the recvmsg may or may not exist */
	if (kio->kio_recvmsg.km_msg) {
		for (i=0; i < kio->kio_recvmsg.km_cnt; i++) {
			rl += kio->kio_recvmsg.km_msg[i].kiov_len; /* Stats */
			KI_FREE(kio->kio_recvmsg.km_msg[i].kiov_base);
		}
		KI_FREE(kio->kio_recvmsg.km_msg);
	}

	/* sendmsg always exists here but doesn't have a PDU_VAL */
	for (sl=0, i=0; i < kio->kio_sendmsg.km_cnt; i++) {
		sl += kio->kio_sendmsg.km_msg[i].kiov_len; /* Stats */
		KI_FREE(kio->kio_sendmsg.km_msg[i].kiov_base);
	}
	KI_FREE(kio->kio_sendmsg.km_msg);

	if (krc == K_OK) {
		double nmn, nmsq;

		/* Key Len and Value Len stats */
		for (kl=0, i=0; i < kv->kv_keycnt; i++) {
			kl += kv->kv_key[i].kiov_len; /* Stats */
		}

		for (vl=0, i=0; i < kv->kv_valcnt; i++) {
			vl += kv->kv_val[i].kiov_len; /* Stats */
		}

		kst->kst_dels.kop_ok++;
#if 1
		if (kst->kst_dels.kop_ok == 1) {
			kst->kst_dels.kop_ssize= sl;
			kst->kst_dels.kop_smsq = 0.0;

			kst->kst_dels.kop_rsize = rl;
			kst->kst_dels.kop_klen = kl;
			kst->kst_dels.kop_vlen = vl;

		} else {
			nmn = kst->kst_dels.kop_ssize +
				(sl - kst->kst_dels.kop_ssize)/kst->kst_dels.kop_ok;

			nmsq = kst->kst_dels.kop_smsq +
				(sl - kst->kst_dels.kop_ssize) * (sl - nmn);
			kst->kst_dels.kop_ssize  = nmn;
			kst->kst_dels.kop_smsq = nmsq;

			kst->kst_dels.kop_rsize = kst->kst_dels.kop_rsize +
				(rl - kst->kst_dels.kop_rsize)/kst->kst_dels.kop_ok;
			kst->kst_dels.kop_klen = kst->kst_dels.kop_klen +
				(kl - kst->kst_dels.kop_klen)/kst->kst_dels.kop_ok;
			kst->kst_dels.kop_vlen = kst->kst_dels.kop_vlen +
				(vl - kst->kst_dels.kop_vlen)/kst->kst_dels.kop_ok;
		}
#endif

		/* stats, boolean as to whether or not to track timestamps*/
		if (KIOP_ISSET((&kst->kst_dels), KOPF_TSTAT)) {
			ktli_gettime(&kio->kio_ts.kiot_comp);

			s_stats_addts(&kst->kst_dels, kio);

		}
	} else {
		kst->kst_dels.kop_err++;
	}

	memset(kio, 0, sizeof(struct kio));
	KI_FREE(kio);

	return (krc);
}


kstatus_t
d_del_generic(int ktd, kv_t *kv, kb_t *kb, int verck)
{
	kstatus_t ks;
	kio_t *kio;

	ks = d_del_aio_generic(ktd, kv, kb, verck, NULL, &kio);
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
		ks = d_del_aio_complete(ktd, kio, NULL);
		if (ks == K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;

	} while (1);

	return(ks);
}


/**
 * kstatus_t
 * ki_aio_del(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain kiovec array representing the value
 * 		kv_vers and kv_verslen are optional
 * 		kv_dival and kv_divalen are optional.
 *		kv_ditype must be set if kv_dival is set
 *		kv_cpolicy sets the caching strategy for this del
 *			cpolicy of flush will flush the entire cache
 *
 * Delete the value specified by the given key, data integrity value/type, and
 * new version, using the specified cache policy. This call will force the
 * del to the new values without any version checks.
 */
kstatus_t
ki_aio_del(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
{
	int verck;
	return(d_del_aio_generic(ktd, kv, (kb_t *)kb, verck=0, cctx, kio));
}


/**
 * ki_del(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val should not be set
 * 		kv_vers and kv_verslen are ignored
 * 		kv_dival and kv_divalen are ignored
 *		kv_ditype must be set if kv_dival is set
 *		kv_cpolicy sets the caching strategy for this del
 *			cpolicy of flush will flush the entire cache
 *
 * Delete the value specified by the given key. This call will force the
 * delete to complete without any version checks.
 */
kstatus_t
ki_del(int ktd, kbatch_t *kb, kv_t *kv)
{
	int verck;
	return(d_del_generic(ktd, kv, (kb_t *)kb, verck=0));
}


/**
 * kstatus_t
 * ki_aio_cad(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain kiovec array representing the value
 * 		kv_vers and kv_verslen are optional
 * 		kv_dival and kv_divalen are optional.
 *		kv_ditype must be set if kv_dival is set
 *		kv_cpolicy sets the caching strategy for this del
 *			cpolicy of flush will flush the entire cache
 *
 * CAD performs a compare and delete for the given key. The key value is
 * only deleted if kv_version matches the version in the DB. If there is
 * no version match the operation fails with the error K_EVERSION.
 * Once a successful version check is complete, the given key and its value
 * are deleted.
 */
kstatus_t
ki_aio_cad(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
{
	int verck;
	return(d_del_aio_generic(ktd, kv, (kb_t *)kb, verck=1, cctx, kio));
}


/**
 * ki_cad(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val should not be set
 * 		kv_vers and kv_verslen are required.
 * 		kv_newvers and kv_newverslen are required.
 * 		kv_disum and kv_disumlen is ignored
 *		kv_ditype must be set if kv_dival is set
 *		kv_cpolicy sets the caching strategy for this del
 *			cpolicy of flush will flush the entire cache
 *
 * CAD performs a compare and delete for the given key. The key value is
 * only deleted if kv_version matches the version in the DB. If there is
 * no version match the operation fails with the error K_EVERSION.
 * Once a successful version check is complete, the given key and its value
 * are deleted.
 */
kstatus_t
ki_cad(int ktd, kbatch_t *kb, kv_t *kv)
{
	int verck;
	return(d_del_generic(ktd, kv, (kb_t *)kb, verck=1));
}

struct kresult_message create_delkey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
											 kv_t *cmd_data, int bool_shouldforce) {

	// declare protobuf structs on stack
	kproto_kv_t proto_cmd_body;
	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

	// extract from cmd_data into proto_cmd_body
	int extract_result = keyname_to_proto(
		&(proto_cmd_body.key), cmd_data->kv_key, cmd_data->kv_keycnt
	);
	proto_cmd_body.has_key = extract_result;

	if (extract_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// to delete we need to specify the dbversion
	set_bytes_optional(&proto_cmd_body, dbversion, cmd_data->kv_ver, cmd_data->kv_verlen);

	// if force is specified, then the dbversion is essentially ignored.
	set_primitive_optional(&proto_cmd_body, force          , bool_shouldforce    );
	set_primitive_optional(&proto_cmd_body, synchronization, cmd_data->kv_cpolicy);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(cmd_hdr, &proto_cmd_body);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	KI_FREE(proto_cmd_body.key.data);

	if (!command_bytes.data) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// return the constructed del message (or failure)
	return create_message(msg_hdr, command_bytes);
}

kstatus_t extract_delkey(struct kresult_message *resp_msg, kv_t *kv_data) {
	// assume failure status
	kstatus_t krc = K_INVALID_SC;
	kproto_msg_t *kv_resp_msg;

	// commandbytes should exist
	kv_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!kv_resp_msg->has_commandbytes) {
		debug_printf("extract_delkey: no resp cmd");
		return(K_EINTERNAL);
	}

	kproto_cmd_t *resp_cmd = unpack_kinetic_command(kv_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_delkey: resp cmd unpack");
		return (K_EINTERNAL);
	}

	// extract the status. On failure, skip to cleanup
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK) {
		debug_printf("extract_delkey: status");
		goto extract_dex;
	}

	// check if there's command data to parse, otherwise cleanup and exit
	if (!resp_cmd->body || !resp_cmd->body->keyvalue) {
		debug_printf("extract_delkey: command missing body or kv");
		goto extract_dex;
	}

	// Since everything seemed successful, let's pop this data on our cleaning stack
	krc = ki_addctx(kv_data, resp_cmd, destroy_command);
	if (krc != K_OK) {
		debug_printf("extract_delkey: destroy context");
		goto extract_dex;
	}

	// ------------------------------
	// begin extraction of command data
	// extract key name, db version, tag, and data integrity algorithm
	kproto_kv_t *resp_kv = resp_cmd->body->keyvalue;

	// NOTE: only modify kv_keycnt if we would change kv_key
	// (otherwise kv_key and kv_keycnt fall out of sync)
	if (resp_kv->has_key) { kv_data->kv_keycnt = 1; }
	extract_bytes_optional(
		kv_data->kv_key->kiov_base, kv_data->kv_key->kiov_len,
		resp_kv, key
	);

	extract_bytes_optional(
		kv_data->kv_ver, kv_data->kv_verlen,
		resp_kv, dbversion
	);

	extract_bytes_optional(
		kv_data->kv_disum, kv_data->kv_disumlen,
		resp_kv, tag
	);

	extract_primitive_optional(kv_data->kv_ditype, resp_kv, algorithm);

	return krc;

 extract_dex:

	destroy_command(resp_cmd);

	// Just make sure we don't return an ok message
	if (krc == K_OK) {
		debug_printf("extract_delkey: error exit");
		krc = K_EINTERNAL;
	}

	return krc;
}
