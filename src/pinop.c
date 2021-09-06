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
struct kresult_message create_pinop_message(kmsghdr_t *, kcmdhdr_t *,
					    kdevop_t op);
kstatus_t extract_pinop(struct kresult_message *resp_msg);


kstatus_t
p_pinop_aio_generic(int ktd, void *pin, size_t pinlen, kdevop_t op,
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
		debug_printf("pinop: kio ptr required");
		return(K_EINVAL);
	}

	/* Clear the callers kio, ckio */
	*ckio = NULL;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("pinop: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	kst = &ses->ks_stats;

	/*
	 * create the kio structure; on failure,
	 * nothing malloc'd so we just return
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		kst->kst_pinops.kop_err++;
		debug_printf("pinop: kio alloc");
		return(K_ENOMEM);
	}
	memset(kio, 0, sizeof(struct kio));

	/*
	 * Setup msg_hdr
	 * This is a PIN auth type no need for the HMAC.
	 *
	 * The pin used here is a caller provided reference  and is 
	 * eventually hung on the kmreq. This reference needs to be 
	 * removed before destroying kmreq. The protobuf destroy code 
	 * will try to clean up this ptr which is an outside
	 * ptr.  See below at pex_req:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype  = KAT_PIN;
	msg_hdr.kmh_pin    = pin;
	msg_hdr.kmh_pinlen = pinlen;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_PINOP;

	kmreq = create_pinop_message(&msg_hdr, &cmd_hdr, op);
	if (kmreq.result_code == FAILURE) {
		debug_printf("pinop: request message create");
		krc = K_EINTERNAL;
		goto pex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= KMT_PINOP;
	kio->kio_flags	= KIOF_INIT;

	/* This is a normal pinop, there is a response */
	KIOF_SET(kio, KIOF_REQRESP);

	/* If timestamp tracking is enabled for this op set it in the KIO */
	if (KIOP_ISSET((&kst->kst_pinops), KOPF_TSTAT)) {
		/* Deal with time stamps */
		KIOF_SET(kio, KIOF_TSTAMP);
		kio->kio_ts.kiot_start = start;
	}

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
		debug_printf("pinop: sendmesg alloc");
		krc = K_ENOMEM;
		goto pex_kmreq;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);

	if (!kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base) {
		debug_printf("pinop: sendmesg PDU alloc");
		krc = K_ENOMEM;
		goto pex_kmmsg;
	}

	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("pinop: sendmesg msg pack");
		krc = K_EINTERNAL;
		goto pex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu,  (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("pinop: PDU(x%2x, %d, %d)\n",
		     pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		debug_printf("pinop: kio send");
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
	 * Tad bit hacky. Need to remove a reference to kmh_pin as that 
	 * is the callers reference.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	*ckio = kio;

	((kproto_msg_t *) kmreq.result_message)->pinauth->pin.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->pinauth->pin.len = 0;
	destroy_message(kmreq.result_message);

	return(K_OK);

	/* Error Exit. */

	/* pex_kmmsg_val:
	 * Nothing to do as del KVs are just keys no values
	 */

 pex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 pex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);

 pex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 pex_kmreq:

	/* 
	 * Tad bit hacky. Need to remove a reference to kmh_pin as that 
	 * is the callers reference.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->pinauth->pin.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->pinauth->pin.len = 0;
	destroy_message(kmreq.result_message);

 pex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

	kst->kst_pinops.kop_err++; /* Record the error in the stats */

	return (krc);
}


/*
 * Complete a AIO pinop call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated.
 */
kstatus_t
p_pinop_aio_complete(int ktd, struct kio *kio, void **cctx)
{
	int rc, i;
	uint32_t sl=0, rl=0, kl=0, vl=0;/* Stats send/recv/key/val lengths */
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
		debug_printf("pinop: kio invalid");
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
			debug_printf("pinop: kio not available");
			return(K_EAGAIN);
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			kst->kst_pinops.kop_err++;
			debug_printf("pinop: kio receive failed");
			return(K_EINTERNAL);
		}
	}

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go.
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("pinop: kio timed out");
		kst->kst_pinops.kop_err++;
		krc = K_ETIMEDOUT;
		goto nex;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("pinop: kio failed");
		kst->kst_pinops.kop_err++;
		krc = K_ENOMSG;
		goto nex;
	}

	/* Got a RECEIVED KIO, validate and decode */

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("pinop: PDU bad length");
		krc = K_EINTERNAL;
		goto nex;
	}
	UNPACK_PDU(&pdu, ((uint8_t *)(kiov->kiov_base)));

	/*
	 * Does the PDU match what was given in the recvmsg
	 * Value is always there even if len = 0
	 */
	kiov = kio->kio_recvmsg.km_msg;
	if ((pdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (pdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		debug_printf("pinop: PDU decode");
		krc = K_EINTERNAL;
		goto nex;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base,
					kiov[KIOV_MSG].kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("pinop: msg unpack");
		krc = K_EINTERNAL;
		goto nex;
	}

	krc = extract_pinop(&kmresp);

	/* clean up */
	destroy_message(kmresp.result_message);

 nex:
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

		/* Key Len and Value Len stats are 0 for PINOP*/
		kl=0;
		vl=0;

		kst->kst_pinops.kop_ok++;
#if 1
		if (kst->kst_pinops.kop_ok == 1) {
			kst->kst_pinops.kop_ssize= sl;
			kst->kst_pinops.kop_smsq = 0.0;

			kst->kst_pinops.kop_rsize = rl;
			kst->kst_pinops.kop_klen = kl;
			kst->kst_pinops.kop_vlen = vl;

		} else {
			nmn = kst->kst_pinops.kop_ssize +
				(sl - kst->kst_pinops.kop_ssize)/kst->kst_pinops.kop_ok;

			nmsq = kst->kst_pinops.kop_smsq +
				(sl - kst->kst_pinops.kop_ssize) * (sl - nmn);
			kst->kst_pinops.kop_ssize  = nmn;
			kst->kst_pinops.kop_smsq = nmsq;

			kst->kst_pinops.kop_rsize = kst->kst_pinops.kop_rsize +
				(rl - kst->kst_pinops.kop_rsize)/kst->kst_pinops.kop_ok;
			kst->kst_pinops.kop_klen = kst->kst_pinops.kop_klen +
				(kl - kst->kst_pinops.kop_klen)/kst->kst_pinops.kop_ok;
			kst->kst_pinops.kop_vlen = kst->kst_pinops.kop_vlen +
				(vl - kst->kst_pinops.kop_vlen)/kst->kst_pinops.kop_ok;
		}
#endif

		/* stats, boolean as to whether or not to track timestamps*/
		if (KIOP_ISSET((&kst->kst_pinops), KOPF_TSTAT)) {
			ktli_gettime(&kio->kio_ts.kiot_comp);

			s_stats_addts(&kst->kst_pinops, kio);

		}
	} else {
		kst->kst_pinops.kop_err++;
	}
	
	memset(kio, 0, sizeof(struct kio));
	KI_FREE(kio);

	return (krc);
}


kstatus_t
p_pinop_generic(int ktd, void *pin, size_t pinlen, kdevop_t op)
{
	kstatus_t ks;
	kio_t *kio;

	ks = p_pinop_aio_generic(ktd, pin, pinlen, op, NULL, &kio);
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
		ks = p_pinop_aio_complete(ktd, kio, NULL);
		if (ks == K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;

	} while (1);

	return(ks);
}


/**
 * kstatus_t
 * ki_aio_pinop(int ktd, void *cctx, kio_t **kio)
 *
 * Perform a round trip PINOP command.
 */
kstatus_t
ki_aio_pinop(int ktd, void *pin, size_t pinlen, kdevop_t op,
	     void *cctx, kio_t **kio)
{
	return(p_pinop_aio_generic(ktd, pin, pinlen, op, cctx, kio));
}


/**
 * ki_pinop(int ktd)
 *
 * Perform a round trip PINOP command.
 */
kstatus_t
ki_pinop(int ktd, void *pin, size_t pinlen, kdevop_t op)
{
	return(p_pinop_generic(ktd, pin, pinlen, op));
}


struct kresult_message
create_pinop_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, kdevop_t op)
{
	kproto_kpinop_t proto_cmd_body;
	ProtobufCBinaryData command_bytes;

	/* declare protobuf structs on stack */
	com__seagate__kinetic__proto__command__pin_operation__init(&proto_cmd_body);

	set_primitive_optional(&proto_cmd_body, pinoptype, op);

	/* construct command bytes to place into message */
	command_bytes = create_command_bytes(cmd_hdr, &proto_cmd_body);
 
	if (!command_bytes.data) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	/* return the constructed del message (or failure) */
	return create_message(msg_hdr, command_bytes);
}


kstatus_t
extract_pinop(struct kresult_message *resp_msg)
{

	kstatus_t krc = K_INVALID_SC;	/* assume failure status */
	kproto_msg_t *pinop_resp_msg;
	kproto_cmd_t *resp_cmd;

	/* commandbytes should exist */
	pinop_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!pinop_resp_msg->has_commandbytes) {
		debug_printf("extract_pinop: no resp cmd");
		return(K_EINTERNAL);
	}

	/*  unpack the command bytes */
	resp_cmd = unpack_kinetic_command(pinop_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_pinop: resp cmd unpack");
		return(K_EINTERNAL);
	}

	/* extract the status. On failure, skip to cleanup */
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK) {
		debug_printf("extract_pinop: status");
	}

	destroy_command(resp_cmd);

	return krc;
}
