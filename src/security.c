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
create_security_pin_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
			    void *pin,  size_t pinlen,
			    void *npin, size_t npinlen, kpin_type_t ptype);

struct kresult_message
create_security_acl_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
			    kacl_t *acl, size_t aclcnt);

	kstatus_t extract_security(struct kresult_message *resp_msg);


kstatus_t
s_security_aio_generic(int ktd,

		       /* Args for PIN Security OP */
		       void *pin,  size_t pinlen,
		       void *npin, size_t npinlen, kpin_type_t ptype,

		       /* Args for ACL Security OP */
		       kacl_t *acl, size_t aclcnt,
		       
		       void *cctx, kio_t **ckio)
{
	int rc, n, aclop;		/* return code, temps */
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
		debug_printf("security: kio ptr required");
		return(K_EINVAL);
	}

	/* Validate args a bit */
	if (acl) {
		/* ACL call */
		aclop = 1;
		if (pin || npin) {
			debug_printf("security: error: both ACL & PIN defined");
			return(K_EINVAL);
		}

		if (!aclcnt) {
			debug_printf("security: error: zero ACL cnt");
			return(K_EINVAL);
		}
	} else {
		/* PIN call, pin may be empty but npin must be defined */
		aclop = 0;
		if (!npin || !npinlen) {
			debug_printf("security: error: nPIN undefined");
					return(K_EINVAL);

		}

		if ((ptype != KPIN_LOCK) && (ptype != KPIN_ERASE)) {
			debug_printf("security: error: Bad pin type");
			return(K_EINVAL);
		}
	}

	/* Clear the callers kio, ckio */
	*ckio = NULL;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("security: ktli config");
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
		kst->kst_securitys.kop_err++;
		debug_printf("security: kio alloc");
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
	 * unfreeable ptr.  See below at sex_req:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype  = KAT_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_SECURITY;

	if (aclop)
		kmreq = create_security_acl_message(&msg_hdr, &cmd_hdr,
						    acl, aclcnt);
	else
		kmreq = create_security_pin_message(&msg_hdr, &cmd_hdr,
						    pin,  pinlen,
						    npin, npinlen, ptype);
	if (kmreq.result_code == FAILURE) {
		debug_printf("security: request message create");
		krc = K_EINTERNAL;
		goto sex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= KMT_SECURITY;
	kio->kio_flags	= KIOF_INIT;

	/* This is a normal security, there is a response */
	KIOF_SET(kio, KIOF_REQRESP);

	/* If timestamp tracking is enabled for this op set it in the KIO */
	if (KIOP_ISSET((&kst->kst_securitys), KOPF_TSTAT)) {
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
		debug_printf("security: sendmesg alloc");
		krc = K_ENOMEM;
		goto sex_kmreq;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);

	if (!kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base) {
		debug_printf("security: sendmesg PDU alloc");
		krc = K_ENOMEM;
		goto sex_kmmsg;
	}

	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("security: sendmesg msg pack");
		krc = K_EINTERNAL;
		goto sex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu,  (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("security: PDU(x%2x, %d, %d)\n",
		     pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		debug_printf("security: kio send");
		krc = K_EINTERNAL;
		goto sex_kmmsg_msg;
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

	/* sex_kmmsg_val:
	 * Nothing to do as del KVs are just keys no values
	 */

 sex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 sex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);

 sex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 sex_kmreq:

	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

 sex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

	kst->kst_securitys.kop_err++; /* Record the error in the stats */

	return (krc);
}


/*
 * Complete a AIO security call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated.
 */
kstatus_t
s_security_aio_complete(int ktd, struct kio *kio, void **cctx)
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
		debug_printf("security: kio invalid");
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
			debug_printf("security: kio not available");
			return(K_EAGAIN);
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			kst->kst_securitys.kop_err++;
			debug_printf("security: kio receive failed");
			return(K_EINTERNAL);
		}
	}

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go.
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("security: kio timed out");
		kst->kst_securitys.kop_err++;
		krc = K_ETIMEDOUT;
		goto nex;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("security: kio failed");
		kst->kst_securitys.kop_err++;
		krc = K_ENOMSG;
		goto nex;
	}

	/* Got a RECEIVED KIO, validate and decode */

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("security: PDU bad length");
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
		debug_printf("security: PDU decode");
		krc = K_EINTERNAL;
		goto nex;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base,
					kiov[KIOV_MSG].kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("security: msg unpack");
		krc = K_EINTERNAL;
		goto nex;
	}

	krc = extract_security(&kmresp);

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

		/* Key Len and Value Len stats are 0 for SECURITY*/
		kl=0;
		vl=0;

		kst->kst_securitys.kop_ok++;
#if 1
		if (kst->kst_securitys.kop_ok == 1) {
			kst->kst_securitys.kop_ssize= sl;
			kst->kst_securitys.kop_smsq = 0.0;

			kst->kst_securitys.kop_rsize = rl;
			kst->kst_securitys.kop_klen = kl;
			kst->kst_securitys.kop_vlen = vl;

		} else {
			nmn = kst->kst_securitys.kop_ssize +
				(sl - kst->kst_securitys.kop_ssize)/kst->kst_securitys.kop_ok;

			nmsq = kst->kst_securitys.kop_smsq +
				(sl - kst->kst_securitys.kop_ssize) * (sl - nmn);
			kst->kst_securitys.kop_ssize  = nmn;
			kst->kst_securitys.kop_smsq = nmsq;

			kst->kst_securitys.kop_rsize = kst->kst_securitys.kop_rsize +
				(rl - kst->kst_securitys.kop_rsize)/kst->kst_securitys.kop_ok;
			kst->kst_securitys.kop_klen = kst->kst_securitys.kop_klen +
				(kl - kst->kst_securitys.kop_klen)/kst->kst_securitys.kop_ok;
			kst->kst_securitys.kop_vlen = kst->kst_securitys.kop_vlen +
				(vl - kst->kst_securitys.kop_vlen)/kst->kst_securitys.kop_ok;
		}
#endif

		/* stats, boolean as to whether or not to track timestamps*/
		if (KIOP_ISSET((&kst->kst_securitys), KOPF_TSTAT)) {
			ktli_gettime(&kio->kio_ts.kiot_comp);

			s_stats_addts(&kst->kst_securitys, kio);

		}
	} else {
		kst->kst_securitys.kop_err++;
	}
	
	memset(kio, 0, sizeof(struct kio));
	KI_FREE(kio);

	return (krc);
}


kstatus_t
s_security_generic(int ktd,
		   void *pin,  size_t pinlen,
		   void *npin, size_t npinlen, kpin_type_t ptype,
		   kacl_t *acl, size_t aclcnt)
		   
{
	kstatus_t ks;
	kio_t *kio;

	ks = s_security_aio_generic(ktd,
				    pin,  pinlen,
				    npin, npinlen, ptype,
				    acl, aclcnt,
				    NULL, &kio);
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
		ks = s_security_aio_complete(ktd, kio, NULL);
		if (ks == K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;

	} while (1);

	return(ks);
}


/**
 * kstatus_t
 * ki_setpin(int ktd, void *pin,  size_t pinlen,
 * 	              void *npin, size_t npinlen, kpin_type_t ptype)
 * 
 *  pin		byte string containing the current pin
 *  pinlen	length of the the current pin
 *  npin	byte string containinf the new pin
 *  npinlen	length of the the new pin
 *  ptype	defines which pin to set: Erase or Lock pin
 *
 * Perform a round trip SECURITY command.
 */
kstatus_t
ki_setpin(int ktd, void *pin, size_t pinlen,
	  void *npin, size_t npinlen, kpin_type_t ptype)
{
	return(s_security_generic(ktd,
				  pin, pinlen, npin, npinlen, ptype,
				  NULL, 0));
}


/**
 * kstatus_t
 * ki_setacls(int ktd, kacl_t *acl, size_t aclcnt)
 * 
 *  acl		Array of kacl_t contain ACLs to be set
 *  aclcnt	Number of element in the acl array 
 *
 * Perform a round trip SECURITY command.
 */
kstatus_t
ki_setacls(int ktd, kacl_t *acl, size_t aclcnt)
{
	/* Currently unsupported */
	return(K_EREJECTED);
#if 0 	
	return(s_security_generic(ktd,
				  NULL, 0, NULL, 0, -1,
				  acl, aclcnt));
#endif
}


struct kresult_message
create_security_pin_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
			    void *pin,  size_t pinlen,
			    void *npin, size_t npinlen, kpin_type_t ptype)
{
	kproto_ksecurity_t proto_cmd_body;
	ProtobufCBinaryData command_bytes;

	/* declare protobuf structs on stack */
	com__seagate__kinetic__proto__command__security__init(&proto_cmd_body);

	/*
	 * BTW: this is stupid kinetic.proto definition. There an op type
	 * that defines if this is an erase or lock op. You can only have one
	 * PIN op per security message. This means a security message will
	 * only carry one set of pins, old and new, per security message.
	 * So you don't need separate old and new pin fields for both, 
	 * oldpin and newpin would be sufficient.
	 */
	if (ptype == KPIN_LOCK) {
		set_primitive_optional(&proto_cmd_body,
				       securityoptype, KSOT_LOCK);
		set_bytes_optional(&proto_cmd_body, oldlockpin,  pin,  pinlen);
		set_bytes_optional(&proto_cmd_body, newlockpin, npin, npinlen);

	} else {
		set_primitive_optional(&proto_cmd_body,
				       securityoptype, KSOT_ERASE);
		set_bytes_optional(&proto_cmd_body, olderasepin,  pin,  pinlen);
		set_bytes_optional(&proto_cmd_body, newerasepin, npin, npinlen);
	}

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


struct kresult_message
create_security_acl_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
			    kacl_t *acl, size_t aclcnt)
{
	kproto_ksecurity_t proto_cmd_body;
	ProtobufCBinaryData command_bytes;

	/* declare protobuf structs on stack */
	com__seagate__kinetic__proto__command__security__init(&proto_cmd_body);

	set_primitive_optional(&proto_cmd_body, securityoptype, KSOT_ACL);

	/* TODO: define ACL and scope messages */

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
extract_security(struct kresult_message *resp_msg)
{

	kstatus_t krc = K_INVALID_SC;	/* assume failure status */
	kproto_msg_t *security_resp_msg;
	kproto_cmd_t *resp_cmd;

	/* commandbytes should exist */
	security_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!security_resp_msg->has_commandbytes) {
		debug_printf("extract_security: no resp cmd");
		return(K_EINTERNAL);
	}

	/*  unpack the command bytes */
	resp_cmd = unpack_kinetic_command(security_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_security: resp cmd unpack");
		return(K_EINTERNAL);
	}

	/* extract the status. On failure, skip to cleanup */
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK) {
		debug_printf("extract_security: status");
	}

	destroy_command(resp_cmd);

	return krc;
}
