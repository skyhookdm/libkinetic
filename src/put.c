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
p_put_aio_generic(int ktd, kv_t *kv, kb_t *kb, int verck,
		  void *cctx, kio_t **ckio)
{
	int rc, i, n, valck;		/* return code, temps, value check */
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
		debug_printf("put: kio ptr required");
		return(K_EINVAL);
	}

	/* Clear the callers kio, ckio */
	*ckio = NULL;

	/* Get KTLI config, Kinetic session and Kinetic stats structure */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("put: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	kst = &ses->ks_stats;

	/* Validate the passed in kv, if forcing a put do no verck */
	rc = ki_validate_kv(kv, verck, (valck=1), &ses->ks_l);
	if (rc < 0) {
		kst->kst_puts.kop_err++;
		debug_printf("put: kv invalid");
		return(K_EINVAL);
	}

	/* Validate the passed in kb, if any */
	rc =  ki_validate_kb(kb, KMT_PUT);
	if (kb && (rc < 0)) {
		kst->kst_puts.kop_err++;
		debug_printf("put: kb invalid");
		return(K_EINVAL);
	}

	/* 
	 * create the kio structure; on failure, 
	 * nothing malloc'd so we just return 
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		kst->kst_puts.kop_err++;
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
	 * be used later on to calculate the actual HMAC which will then 
	 * replace the HMAC key on the kmh_hmac field. 
	 *
	 * A reference is made to the kcfg_hkey ptr in the kmreq. This 
	 * reference needs to be removed before destroying kmreq.  The
	 * protobuf code will try to clean up this ptr which is an outside
	 * unfreeable ptr.  See below at pex_req:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KAT_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_PUT;

	/* if necessary setup the batchid before creating the mesg */
	if (kb) {
		cmd_hdr.kch_bid = kb->kb_bid;
	}

	/* 
	 * Default put checks the version strings, if they don't match
	 * put fails.  Forcing the put avoids the version check. So if 
	 * checking the version, no forced put.
	 */
	kmreq = create_put_message(&msg_hdr, &cmd_hdr, kv, (verck?0:1));
	if (kmreq.result_code == FAILURE) {
		debug_printf("put: request message create");
		krc = K_EINTERNAL;
		goto pex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= KMT_PUT;
	kio->kio_flags	= KIOF_INIT;
	
	if (kb)
		/* This is a batch put, there is no response */
		KIOF_SET(kio, KIOF_REQONLY);
	else
		/* This is a normal put, there is a response */
		KIOF_SET(kio, KIOF_REQRESP);

	/* If timestamp tracking is enabled for this op set it in the KIO */
	if (KIOP_ISSET((&kst->kst_puts), KOPF_TSTAT)) {
		/* Deal with time stamps */
		KIOF_SET(kio, KIOF_TSTAMP);
		kio->kio_ts.kiot_start = start;
	}

	kio->kio_ckv	= kv;		/* Hang the callers kv */
	kio->kio_ckb	= kb;		/* Hang the callers kb, if any */
	kio->kio_cctx	= cctx;		/* Hang the callers context */

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
		goto pex_kmreq;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);

	if (!kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base) {
		debug_printf("put: sendmesg PDU alloc");
		krc = K_ENOMEM;
		goto pex_kmmsg;
	}

	/*
	 * copy the passed in value vector(s) onto the sendmsg,
	 * no value data is copied
	 */
	memcpy(&(kio->kio_sendmsg.km_msg[KIOV_VAL]), kv->kv_val,
	       (sizeof(struct kiovec) * kv->kv_valcnt));

	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("put: sendmesg msg pack");
		krc = K_EINTERNAL;
		goto pex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;

	/* for kp_vallen, need to run through kv_val vector and add it up */
	pdu.kp_vallen = 0;
	for (i = 0;i < kv->kv_valcnt; i++) {
		pdu.kp_vallen += kv->kv_val[i].kiov_len;
	}
	PACK_PDU(&pdu, (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("put: PDU(x%2x, %d, %d)\n",
		     pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

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
			goto pex_kmmsg_msg;
		}
		if ((ses->ks_l.kl_batopscnt > 0) &&
		    (kb->kb_ops > ses->ks_l.kl_batopscnt)) {
			debug_printf("put: batch ops");
			krc = K_EBATCH;
			goto pex_kmmsg_msg;

		}
	}

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		debug_printf("put: kio send");
		krc = K_EINTERNAL;
		goto pex_kmmsg_msg;
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

	/* pex_kmmsg_val:
	 * Nothing to do as the kv val buffers were only hung here,
	 * no allocations or copies made
	 */

 pex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 pex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);

 pex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 pex_kmreq:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

 pex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

	kst->kst_puts.kop_err++; /* Record the error in the stats */

	return (krc);
}


/*
 * Complete a AIO put/cas call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated. 
 */
kstatus_t
p_put_aio_complete(int ktd, struct kio *kio, void **cctx)
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
		debug_printf("put: kio invalid");
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
			debug_printf("put: kio not available");
			return(K_EAGAIN);
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			kst->kst_puts.kop_err++;
			debug_printf("put: kio receive failed");
			return(K_EINTERNAL);
		}
	}

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go. 
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("put: kio timed out");
		kst->kst_puts.kop_err++;
		krc = K_ETIMEDOUT;
		goto pex;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("put: kio failed");
		kst->kst_puts.kop_err++;
		krc = K_ENOMSG;
		goto pex;
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

	/* Special case a batch put handling */
	if (kb) {
		krc = K_OK;

#ifdef KBATCH_SEQTRACKING
		/* 
		 * Batch receives only the sendmsg KIO back, no resp.
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
			goto pex;
		}

		krc = extract_cmdhdr(&kmbat, &cmd_hdr);
		if (krc == K_OK) {
			/* Preserve the req on the batch */
			b_batch_addop(kb, &cmd_hdr);
		}
		
		destroy_message(kmbat.result_message);
#endif /* KBATCH_SEQTRACKING */

		/* normal exit, jump past all response handling */
		goto pex;
	}
	       
	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("put: PDU bad length");
		krc = K_EINTERNAL;
		goto pex;
	}
	UNPACK_PDU(&pdu, ((uint8_t *)(kiov->kiov_base)));

	/* 
	 * Does the PDU match what was given in the recvmsg
	 * Value is always there even if len = 0 
	 */
	kiov = kio->kio_recvmsg.km_msg;
	if ((pdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (pdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		debug_printf("put: PDU decode");
		krc = K_EINTERNAL;
		goto pex;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base,
					kiov[KIOV_MSG].kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("put: msg unpack");
		krc = K_EINTERNAL;
		goto pex;
	}

	krc = extract_putkey(&kmresp, kv);

	/* clean up */
	destroy_message(kmresp.result_message);

pex:
	/* depending on errors the recvmsg may or may not exist */
	if (kio->kio_recvmsg.km_msg) {
		for (rl=0, i=0; i < kio->kio_recvmsg.km_cnt; i++) {
			rl += kio->kio_recvmsg.km_msg[i].kiov_len; /* Stats */

			KI_FREE(kio->kio_recvmsg.km_msg[i].kiov_base);
		}
		KI_FREE(kio->kio_recvmsg.km_msg);
	}

	/*
	 * sendmsg always exists here and has a 1 or more KIOV_VAL vectors
	 * but the value vector(s) are the callers and cannot be freed
	 */
	for (sl=0, i=0; i < kio->kio_sendmsg.km_cnt; i++) {
		sl += kio->kio_sendmsg.km_msg[i].kiov_len; /* Stats */
		if (i < KM_CNT_NOVAL)
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

		kst->kst_puts.kop_ok++;
#if 1
		if (kst->kst_puts.kop_ok == 1) {
			kst->kst_puts.kop_ssize= sl;
			kst->kst_puts.kop_smsq = 0.0;

			kst->kst_puts.kop_rsize = rl;
			kst->kst_puts.kop_klen = kl;
			kst->kst_puts.kop_vlen = vl;
		} else {
			nmn = kst->kst_puts.kop_ssize +
				(sl - kst->kst_puts.kop_ssize)/kst->kst_puts.kop_ok;

			nmsq = kst->kst_puts.kop_smsq +
				(sl - kst->kst_puts.kop_ssize) * (sl - nmn);
			kst->kst_puts.kop_ssize  = nmn;
			kst->kst_puts.kop_smsq = nmsq;

			kst->kst_puts.kop_rsize = kst->kst_puts.kop_rsize +
				(rl - kst->kst_puts.kop_rsize)/kst->kst_puts.kop_ok;
			kst->kst_puts.kop_klen = kst->kst_puts.kop_klen +
				(kl - kst->kst_puts.kop_klen)/kst->kst_puts.kop_ok;
			kst->kst_puts.kop_vlen = kst->kst_puts.kop_vlen +
				(vl - kst->kst_puts.kop_vlen)/kst->kst_puts.kop_ok;
		}
#endif

		/* stats, boolean as to whether or not to track timestamps*/
		if (KIOP_ISSET((&kst->kst_puts), KOPF_TSTAT)) {
			ktli_gettime(&kio->kio_ts.kiot_comp);

			s_stats_addts(&kst->kst_puts, kio);

		}
	} else {
		kst->kst_puts.kop_err++;
	}

	memset(kio, 0, sizeof(struct kio));
	KI_FREE(kio);

	return (krc);
}


kstatus_t
p_put_generic(int ktd, kv_t *kv, kb_t *kb, int verck)
{
	kstatus_t ks;
	kio_t *kio;
			
	ks = p_put_aio_generic(ktd, kv, kb, verck, NULL, &kio);
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
		ks = p_put_aio_complete(ktd, kio, NULL);
		if (ks == K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;
			
	} while (1);	

	return(ks);
}


/**
 * kstatus_t
 * ki_aio_put(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
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
ki_aio_put(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
{
	int verck;
	return(p_put_aio_generic(ktd, kv, (kb_t *)kb, verck=0, cctx, kio));
}


/**
 * kstatus_t
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
 * kstatus_t
 * ki_aio_cas(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
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
ki_aio_cas(int ktd, kbatch_t *kb, kv_t *kv, void *cctx, kio_t **kio)
{
	int verck;
	return(p_put_aio_generic(ktd, kv, (kb_t *)kb, verck=1, cctx, kio));
}


/**
 * kstatus_t
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

	// return the constructed put message (or failure)
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
