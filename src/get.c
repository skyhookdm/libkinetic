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
#include "protocol_interface.h"

/**
 * Internal prototypes
 */
struct kresult_message create_getkey_message(kmsghdr_t *, kcmdhdr_t *, kv_t *);


kstatus_t
g_get_aio_generic(int ktd, kv_t *kv, kv_t *altkv,
		  kmtype_t msg_type, void *cctx, kio_t **ckio)
{
	int rc, verck;
	kstatus_t krc;
	struct kio *kio;		/* Passed pack KIO */
	ksession_t *ses;		/* KTLI Session ptr  */
	kmsghdr_t msg_hdr;
	kcmdhdr_t cmd_hdr;
	struct ktli_config *cf;		/* KTLI configuration */
	struct kresult_message kmreq;	/* Intermediate req representation */
	kpdu_t pdu;			/* Unpacked PDU Structure */
	
	/* Clear the callers kio, ckio */
	*ckio = NULL;
	
	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		return kstatus_err(K_EREJECTED,
				   KI_ERR_BADSESS, "get: ktli config");
	}

	ses = (ksession_t *) cf->kcfg_pconf;

	/* Validate command */
	switch (msg_type) {
	case KMT_GETNEXT:
	case KMT_GETPREV:
		if (!altkv) {
			return kstatus_err(K_EINVAL,
					   KI_ERR_INVARGS, "get: validation");
		}
		
		/* no break here, all cmds need a kv ptr, so fall through */

	case KMT_GET:
	case KMT_GETVERS:
		if (!kv) {
			return kstatus_err(K_EINVAL,
					   KI_ERR_INVARGS, "get: validation");
		}
		break;

	default:
		return kstatus_err(K_EREJECTED,
				   KI_ERR_INVARGS, "get: bad command");
	}

	/* Validate the passed in kv and if necessary the altkv */
	/* verck=0 to ignore version field in the check */
	rc = ki_validate_kv(kv, (verck=0), &ses->ks_l);
	if (rc < 0) {
		return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: invalid kv");
	}

	if (altkv) {
		/* verck=0 to ignore version field in the check */
		rc = ki_validate_kv(altkv, (verck=0), &ses->ks_l);
		if (rc < 0) {
			return kstatus_err(K_EINVAL,
					   KI_ERR_INVARGS, "get: invalid altkv");
		}
	}

	/* 
	 * create the kio structure; on failure, 
	 * nothing malloc'd so we just return 
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get: kio");
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
	 * unfreeable ptr.  See below at gex_req:
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
		goto gex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= msg_type;
	kio->kio_flags	= KIOF_INIT;

	KIOF_SET(kio, KIOF_REQRESP);	/* Normal RPC KIO */

	kio->kio_ckv	= kv;		/* Hang the callers kv */
	kio->kio_caltkv	= altkv;	/* Hang the callers altkv */
	kio->kio_cctx	= cctx;		/* Hang the callers context */

	/* 
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(
		sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt
	);

	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get: request");
		goto gex_kmreq;
	}

	/* Allocate the Packed PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);
	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get: PDU");
		goto gex_kmmsg;
	}


	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		errno = K_EINTERNAL;
		krc   = kstatus_err(errno, KI_ERR_MSGPACK, "get: pack msg");
		goto gex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu, (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("g_get_generic: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		errno = K_EINTERNAL;
		krc   = kstatus_err(errno, KI_ERR_MSGPACK, "get: send msg");
		goto gex_kmmsg_msg;
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

	return(kstatus_err(K_OK, KI_ERR_NOMSG, ""));

	/* Error Exit. */
 gex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 gex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	
 gex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 gex_kmreq:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len = 0;

	destroy_message(kmreq.result_message);

 gex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

	return (krc);
}

/*
 * Complete a AIO get* call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated. 
 */
kstatus_t
g_get_aio_complete(int ktd, struct kio *kio, void **cctx)
{
	int rc;
	kv_t *kv, *altkv;		/* Set to KVs passed in orig aio call */
	kpdu_t pdu;			/* Unpacked PDU Structure */
	kstatus_t krc;			/* Returned status */
	struct kiovec *kiov;		/* Message KIO vector */
	struct kresult_message kmresp;	/* Intermediate resp representation */
	
	/* Setup in case of an error return */
	if (cctx)
		*cctx = NULL; 

	if (!kio  || (kio && (kio->kio_magic !=  KIO_MAGIC))) {
		return(kstatus_err(K_EINVAL,
				   KI_ERR_RECVMSG, "get: bad kio"));
	}
	
	rc = ktli_receive(ktd, kio);
	if (rc < 0) {
		if (errno == ENOENT) {
			/* No available response, so try again */
			return(kstatus_err(K_EAGAIN,
					   KI_ERR_RECVMSG, "get: eagain"));
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			return(kstatus_err(K_EINTERNAL,
					   KI_ERR_RECVMSG, "get: receive"));
		}
	}

	
	/* 
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO 
	 * that is really in an error state, in those cases clean up the KIO 
	 * and go. 
	 */
	if (kio->kio_state != KIO_RECEIVED) {
		krc = kstatus_err(K_EINTERNAL,
				  KI_ERR_RECVMSG, "get: KIO bad state");
		goto gex;
	}		

	/* Got a RECEIVED KIO, validate and decode */
	
	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		krc = kstatus_err(K_EINTERNAL,
				  KI_ERR_RECVPDU, "get: extract PDU");
		goto gex;
	}
	UNPACK_PDU(&pdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
		kiov = kio->kio_recvmsg.km_msg;
	if ((pdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (pdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		krc = kstatus_err(K_EINTERNAL,
				  KI_ERR_PDUMSGLEN, "get: parse pdu");
		goto gex;
	}

	// unpack the message; KIOV_MSG may contain both msg and value
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base, pdu.kp_msglen);
	if (kmresp.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL,
				  KI_ERR_MSGUNPACK, "get: unpack msg");
		goto gex;
	}

	/*
	 * Grab the original KVs sent in from the caller. Although these are 
	 * not directly passed back in the complete, the caller should have 
	 * maintained them across the originating aio call and the complete.
	 */
	kv = kio->kio_ckv;
	altkv = kio->kio_caltkv;

	/*
	 * Grab the value and hang it on either the kv or altkv as approriate
	 * Also extract the kv or altkv data from the response
	 * On extraction failure, we fail the overall complete of this KIO
	 */
	switch (kio->kio_cmd) {
	case KMT_GET:
		/* grab the value */
		kv->kv_val[0].kiov_base = kiov[KIOV_VAL].kiov_base;
		kv->kv_val[0].kiov_len  = kiov[KIOV_VAL].kiov_len;

		/* falls through to KMT_GETVERS to extract command key data */

	case KMT_GETVERS:
		/* if the KV is unfilled out, return vers, disum and ditype */
		krc = extract_getkey(&kmresp, kv);
		break;

	case KMT_GETNEXT:
	case KMT_GETPREV:
		altkv->kv_val[0].kiov_base = kiov[KIOV_VAL].kiov_base;
		altkv->kv_val[0].kiov_len  = kiov[KIOV_VAL].kiov_len;

		/* return the new kv, vers, disum and ditype  in altkv */
		krc = extract_getkey(&kmresp, altkv);
		break;

	default:
		debug_printf("get: should not get here\n");
		assert(0);
		break;
	}

	/* if Success so return the callers context */
	if ((krc.ks_code == (kstatus_code_t )K_OK) && (cctx))
		*cctx = kio->kio_cctx;

	/* clean up */
	destroy_message(kmresp.result_message);

 gex:
	/* depending on errors the recvmsg may or may not exist */
	if (kio->kio_recvmsg.km_msg) {
		if ((kio->kio_recvmsg.km_cnt > KIOV_PDU) &&
		    kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base)
			KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);

		if ((kio->kio_recvmsg.km_cnt > KIOV_MSG) &&
		    kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base)
			KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);

		/* Leave the value buffer for the caller if K_OK */
		if ((kio->kio_recvmsg.km_cnt > KIOV_VAL) &&
		    kio->kio_recvmsg.km_msg[KIOV_VAL].kiov_base &&
		    krc.ks_code != (kstatus_code_t)K_OK)
			KI_FREE(kio->kio_recvmsg.km_msg[KIOV_VAL].kiov_base);

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
	kstatus_t ks;
	kio_t *kio;
			
	ks = g_get_aio_generic(ktd, kv, altkv, msg_type, NULL, &kio);
	if (ks.ks_code != (kstatus_code_t)K_OK) {
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
		ks = g_get_aio_complete(ktd, kio, NULL);
		if (ks.ks_code == (kstatus_code_t)K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;
			
	} while (1);	

	return(ks);
}

#if 0
kstatus_t
g_get_generic(int ktd, kv_t *kv,  kv_t *altkv, kmtype_t msg_type)
{
	int rc, verck;
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
	/* verck=0 to ignore version field in the check */
	rc = ki_validate_kv(kv, (verck=0), &ses->ks_l);
	if (rc < 0) {
		return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: invalid kv");
	}

	if (altkv) {
		/* verck=0 to ignore version field in the check */
		rc = ki_validate_kv(altkv, (verck=0), &ses->ks_l);
		if (rc < 0) {
			return kstatus_err(K_EINVAL, KI_ERR_INVARGS, "get: invalid kv");
		}
	}

	/* 
	 * create the kio structure; on failure, 
	 * nothing malloc'd so we just return 
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get: kio");
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

	/* Setup the KIO */
	kio->kio_cmd            = msg_type;
	kio->kio_flags		= KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP);		/* Normal RPC */

	/* 
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(
		sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt
	);

	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "get: request");
		goto gex_kio;
	}

	/* Hang the Packed PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) &ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len = KP_PLENGTH;

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
		krc   = kstatus_err(errno, KI_ERR_MSGPACK, "get: pack msg");
		goto gex_sendmsg;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu, ppdu);
	debug_printf("g_get_generic: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	ktli_send(ktd, kio);
	debug_printf("Sent Kio: %p\n", kio);
	
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
			} else {
				/* PAK: need to exit, receive failed */
				krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVMSG,
						  "get: recvmsg");
				goto gex_sendmsg;
			}
		}

		/* Got our response */
		break;
	} while (1);

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVPDU, "get: extract PDU");
		goto gex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = kio->kio_recvmsg.km_msg;
	if ((rpdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (rpdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		    krc = kstatus_err(K_EINTERNAL, KI_ERR_PDUMSGLEN,
				      "get: parse pdu");
		goto gex_recvmsg;
	}

	// unpack the message; KIOV_MSG may contain both msg and value
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base, rpdu.kp_msglen);
	if (kmresp.result_code == FAILURE) {
		errno = K_EINTERNAL;
		krc   = kstatus_err(errno, KI_ERR_MSGUNPACK, "get: unpack msg");
		goto gex_resp;
	}

	/*
	 * Grab the value and hang it on either the kv or altkv as approriate
	 * Also extract the kv data from response
	 * On extraction failure, both the response and request need to be 
	 * cleaned up
	 */
	switch (msg_type) {
	case KMT_GET:
		kv->kv_val[0].kiov_base = kiov[KIOV_VAL].kiov_base;
		kv->kv_val[0].kiov_len  = kiov[KIOV_VAL].kiov_len;

		/* falls through to KMT_GETVERS to extract command data */

	case KMT_GETVERS:
		krc = extract_getkey(&kmresp, kv);
		//if (krc.ks_code != K_OK) { kv->destroy_protobuf(kv); }

		break;

	case KMT_GETNEXT:
	case KMT_GETPREV:
		altkv->kv_val[0].kiov_base = kiov[KIOV_VAL].kiov_base;
		altkv->kv_val[0].kiov_len  = kiov[KIOV_VAL].kiov_len;

		krc = extract_getkey(&kmresp, altkv);
		//if (krc.ks_code != K_OK) { altkv->destroy_protobuf(altkv); }

		break;

	default:
		debug_printf("get: should not get here\n");
		assert(0);
		break;
	}

	/* clean up */
 gex_resp:
	destroy_message(kmresp.result_message);

 gex_recvmsg:

	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	/*
	 * This is for the caller to free as it is hung on the kv->kv_val
	 * 	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_VAL].kiov_base);
	 */
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

#endif

/**
 * ki_aio_get(int ktd, kv_t *kv, void *cctx, kio_t **kio)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_disum and kv_disumlen are optional.
 *		kv_ditype is returned by the server, but it should
 * 		have either a 0 or a valid ditype in it to start with
 *  cctx	caller provided context, completely opaque to this call
 *		passed back to the caller in the complete call
 *  ckio 	returned back KIO ptr
 *
 * Initiate getting the value specified by the given key.
 *
 */
kstatus_t
ki_aio_get(int ktd, kv_t *key, void *cctx, kio_t **ckio)
{
	return(g_get_aio_generic(ktd, key, NULL, KMT_GET, cctx, ckio));
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
	kproto_kv_t proto_cmd_body;
	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

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
	ProtobufCBinaryData command_bytes = create_command_bytes(cmd_hdr, &proto_cmd_body);
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
	destroy_command(kv_data->kv_protobuf);
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
	if (!response_cmd) { return kv_status; }
	kv_data->kv_protobuf = response_cmd;

	// extract the response status to be returned. prepare this early to make cleanup easy
	kv_status = extract_cmdstatus(response_cmd);
	if (kv_status.ks_code != (kstatus_code_t) K_OK) { goto extract_gex; }

	// ------------------------------
	// begin extraction of command body into kv_t structure
	if (!response_cmd->body || !response_cmd->body->keyvalue) { goto extract_gex; }
	kproto_kv_t *response = response_cmd->body->keyvalue;

	// extract key name, db version, tag, and data integrity algorithm
	// Only extract if ptr is NULL, other passed in ptrs maybe lost.
	// Caller must clear the structure is they want it filled in
	if (!kv_data->kv_key->kiov_base && response->has_key) {

		// we set the number of keys to 1,
		// since this is not a range request
		kv_data->kv_keycnt = 1;
		
		extract_bytes_optional(kv_data->kv_key->kiov_base,
				       kv_data->kv_key->kiov_len,
				       response, key);
	}

	if (!kv_data->kv_ver) {
		extract_bytes_optional(kv_data->kv_ver, kv_data->kv_verlen,
				       response, dbversion);
	}

	if (!kv_data->kv_disum) {
		extract_bytes_optional(kv_data->kv_disum, kv_data->kv_disumlen,
				       response, tag);
		extract_primitive_optional(kv_data->kv_ditype,
					   response, algorithm);
	}

	// set destructor to be called later
	kv_data->destroy_protobuf = destroy_protobuf_getkey;

	return kv_status;

 extract_gex:
	// call destructor to cleanup
	destroy_protobuf_getkey(kv_data);

	// Just make sure we don't return an ok message
	if (kv_status.ks_code == (kstatus_code_t) K_OK) { kv_status.ks_code = K_EINTERNAL; }

	return kv_status;
}
