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

#if defined(__APPLE__)
    #include <machine/endian.h>
#else
    #include <endian.h>
#endif

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"
#include "protocol_interface.h"


/**
 * Internal prototypes
 */
struct kresult_message
create_exec_message(kmsghdr_t *, kcmdhdr_t *, kapplet_t *);

kstatus_t
extract_exec_response(struct kresult_message *resp_msg, kapplet_t *app);


kstatus_t
e_exec_aio_generic(int ktd, kapplet_t *app, void *cctx, kio_t **ckio)
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
	 * Sending an op, record the clock. The session is not known yet.
	 * This is the begining of the send and the natural spot to
	 * start the clock on send processing time. Without knowing
	 * the session info it is not known if the code should be recording
	 * timestamps. So this maybe wasted code. However vdso(7) makes
	 * this fast, Not going to worry about this.
	 */
	ktli_gettime(&start);

	if (!ckio) {
		debug_printf("exec: kio ptr required");
		return(K_EINVAL);
	}

	/* Clear the callers kio, ckio */
	*ckio = NULL;

	/* Get KTLI config, Kinetic session and Kinetic stats structure */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("exec: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	kst = &ses->ks_stats;

	/* Validate the passed in kv, if forcing a exec do no verck */
	rc = ki_validate_kapplet(app, &ses->ks_l);
	if (rc < 0) {
		kst->kst_execs.kop_err++;
		debug_printf("exec: app invalid");
		return(K_EINVAL);
	}

	/* 
	 * create the kio structure; on failure, 
	 * nothing malloc'd so we just return 
	 */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		kst->kst_execs.kop_err++;
		debug_printf("exec: kio alloc");
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
	 * unfreeable ptr.  See below at eex_req:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KAT_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type = KMT_APPLET;

	/* 
	 * Default exec checks the version strings, if they don't match
	 * exec fails.  Forcing the exec avoids the version check. So if 
	 * checking the version, no forced exec.
	 */
	kmreq = create_exec_message(&msg_hdr, &cmd_hdr, app);
	if (kmreq.result_code == FAILURE) {
		debug_printf("exec: request message create");
		krc = K_EINTERNAL;
		goto eex_kio;
	}

	/* Setup the KIO */
	kio->kio_magic	= KIO_MAGIC;
	kio->kio_cmd	= KMT_APPLET;
	kio->kio_flags	= KIOF_INIT;
	
	/* KMT_APPLET is a REQRESP */
	KIOF_SET(kio, KIOF_REQRESP);

	/* If timestamp tracking is enabled for this op set it in the KIO */
	if (KIOP_ISSET((&kst->kst_execs), KOPF_TSTAT)) {
		/* Deal with time stamps */
		KIOF_SET(kio, KIOF_TSTAMP);
		kio->kio_ts.kiot_start = start;
	}

	kio->kio_ckapp	= app;		/* Hang the callers app */
	kio->kio_cctx	= cctx;		/* Hang the callers context */

	/*
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message, and then elements 2 and beyond are
	 * for the value. The size is variable as the value can come in
	 * many parts from the caller. 
	 * See kio.h (previously in message.h) for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);

	if (!kio->kio_sendmsg.km_msg) {
		debug_printf("exec: sendmesg alloc");
		krc = K_ENOMEM;
		goto eex_kmreq;
	}

	/* Hang the PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = KI_MALLOC(KP_PLENGTH);

	if (!kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base) {
		debug_printf("exec: sendmesg PDU alloc");
		krc = K_ENOMEM;
		goto eex_kmmsg;
	}

	/* pack the message and hang it on the kio */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("exec: sendmesg msg pack");
		krc = K_EINTERNAL;
		goto eex_kmmsg_pdu;
	}

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len;
	pdu.kp_vallen = 0;

	PACK_PDU(&pdu, (uint8_t *)kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);
	debug_printf("exec: PDU(x%2x, %d, %d)\n",
		     pdu.kp_magic, pdu.kp_msglen, pdu.kp_vallen);

	/* Send the request */
	if (ktli_send(ktd, kio) < 0) {
		debug_printf("exec: kio send");
		krc = K_EINTERNAL;
		goto eex_kmmsg_msg;
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

	/* eex_kmmsg_val:
	 * Nothing to do as the kv val buffers were only hung here,
	 * no allocations or copies made
	 */

 eex_kmmsg_msg:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);

 eex_kmmsg_pdu:
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base);

 eex_kmmsg:
	KI_FREE(kio->kio_sendmsg.km_msg);

 eex_kmreq:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len  = 0;

	destroy_message(kmreq.result_message);

 eex_kio:
	kio->kio_magic = 0; /* clear the kio magic  in case this lives on */
	KI_FREE(kio);

	kst->kst_execs.kop_err++; /* Record the error in the stats */

	return (krc);
}


/*
 * Complete a AIO exec call.
 * Any error other no available response, the KIO should be cleaned up
 * and terminated. 
 */
kstatus_t
e_exec_aio_complete(int ktd, struct kio *kio, void **cctx)
{
	int rc, i;
	uint32_t sl=0, rl=0;		/* Stats send/recv lengths */
	kapplet_t *app;			/* Set to App passed in orig aio call */
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
		debug_printf("exec: kio invalid");
		return(K_EINVAL);
	}

	/* Get KTLI config, Kinetic session and Kinetic stats structure */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("exec: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	kst = &ses->ks_stats;

	rc = ktli_receive(ktd, kio);
	if (rc < 0) {
		if (errno == ENOENT) {
			/* No available response, so try again */
			debug_printf("exec: kio not available");
			return(K_EAGAIN);
		} else {
			/* Receive really failed
			 * KTLI contract is that if error is returned no KIO
			 * was found. Success means a KIO was found and control
			 * of that KIO was returned to caller.
			 * Hence, this error means nothing to clean up
			 */
			kst->kst_execs.kop_err++;
			debug_printf("exec: kio receive failed");
			return(K_EINTERNAL);
		}
	}

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go. 
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("exec: kio timed out");
		kst->kst_execs.kop_err++;
		krc = K_ETIMEDOUT;
		goto eex;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("exec: kio failed");
		kst->kst_execs.kop_err++;
		krc = K_ENOMSG;
		goto eex;
	}

	/* Got a RECEIVED KIO, validate and decode */

	/*
	 * Grab the original KAPPLET sent in from the caller. 
	 * Although these are not directly passed back in the complete, 
	 * the caller should have maintained them across the originating 
	 * aio call and the complete.
	 */
	app = kio->kio_ckapp;

	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("exec: PDU bad length");
		krc = K_EINTERNAL;
		goto eex;
	}
	UNPACK_PDU(&pdu, ((uint8_t *)(kiov->kiov_base)));

	/* 
	 * Does the PDU match what was given in the recvmsg
	 * Value is always there even if len = 0 
	 */
	kiov = kio->kio_recvmsg.km_msg;
	if ((pdu.kp_msglen != kiov[KIOV_MSG].kiov_len) ||
	    (pdu.kp_vallen != kiov[KIOV_VAL].kiov_len))    {
		debug_printf("exec: PDU decode");
		krc = K_EINTERNAL;
		goto eex;
	}

	/* Now unpack the message */
	kmresp = unpack_kinetic_message(kiov[KIOV_MSG].kiov_base,
					kiov[KIOV_MSG].kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("exec: msg unpack");
		krc = K_EINTERNAL;
		goto eex;
	}

	/* Grab the value if necessary */
	if (app->ka_outkey) {
		app->ka_outkey->kv_val[0].kiov_base = kiov[KIOV_VAL].kiov_base;
		app->ka_outkey->kv_val[0].kiov_len  = kiov[KIOV_VAL].kiov_len;
	}

	/* Now extract the results */
	krc = extract_exec_response(&kmresp, app);

	/* clean up */
	destroy_message(kmresp.result_message);

eex:
	/* depending on errors the recvmsg may or may not exist */
	if (kio->kio_recvmsg.km_msg) {
		for (rl=0, i=0; i < kio->kio_recvmsg.km_cnt; i++) {
			rl += kio->kio_recvmsg.km_msg[i].kiov_len; /* Stats */

			/* 
			 * Free recvmsg vectors
			 * if there is a kiov_base ptr then
			 * PDU and MSG vectors get freed (i<KM_CNT_NOVAl)
			 * VAL vectors only get freed if there is an error
			 * Otherwise VAL vectors are returned to caller
			 * Caller is then responsible
			 */
			if (((i < KM_CNT_NOVAL) || (krc != K_OK)) &&
			    (kio->kio_recvmsg.km_msg[i].kiov_base)){
				KI_FREE(kio->kio_recvmsg.km_msg[i].kiov_base);
			} 

		}
		KI_FREE(kio->kio_recvmsg.km_msg);
	}

	/*
	 * sendmsg always exists here and has 0 KIOV_VAL vectors
	 */
	for (sl=0, i=0; i < kio->kio_sendmsg.km_cnt; i++) {
		sl += kio->kio_sendmsg.km_msg[i].kiov_len; /* Stats */
		if (kio->kio_sendmsg.km_msg[i].kiov_base)
			KI_FREE(kio->kio_sendmsg.km_msg[i].kiov_base);
	}
	KI_FREE(kio->kio_sendmsg.km_msg);

	if (krc == K_OK) {
		double nmn, nmsq;

		kst->kst_execs.kop_ok++;
#if 1
		if (kst->kst_execs.kop_ok == 1) {
			kst->kst_execs.kop_ssize= sl;
			kst->kst_execs.kop_smsq = 0.0;

			kst->kst_execs.kop_rsize = rl;
		} else {
			nmn = kst->kst_execs.kop_ssize +
				(sl - kst->kst_execs.kop_ssize)/kst->kst_execs.kop_ok;

			nmsq = kst->kst_execs.kop_smsq +
				(sl - kst->kst_execs.kop_ssize) * (sl - nmn);
			kst->kst_execs.kop_ssize  = nmn;
			kst->kst_execs.kop_smsq = nmsq;

			kst->kst_execs.kop_rsize = kst->kst_execs.kop_rsize +
				(rl - kst->kst_execs.kop_rsize)/kst->kst_execs.kop_ok;
		}
#endif

		/* stats, boolean as to whether or not to track timestamps*/
		if (KIOP_ISSET((&kst->kst_execs), KOPF_TSTAT)) {
			ktli_gettime(&kio->kio_ts.kiot_comp);

			s_stats_addts(&kst->kst_execs, kio);

		}
	} else {
		kst->kst_execs.kop_err++;
	}

	memset(kio, 0, sizeof(struct kio));
	KI_FREE(kio);

	return (krc);
}


kstatus_t
e_exec_generic(int ktd, kapplet_t *app)
{
	kstatus_t ks;
	kio_t *kio;
			
	ks = e_exec_aio_generic(ktd, app, NULL, &kio);
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
		ks = e_exec_aio_complete(ktd, kio, NULL);
		if (ks == K_EAGAIN) continue;

		/* Found the key or an error occurred, time to go */
		break;
			
	} while (1);	

	return(ks);
}


/**
 * kstatus_t
 * ki_aio_exec(int ktd,  kapplet_t *app, void *cctx, kio_t **kio)
 *
 *  app		contains
 *
 * Exec the function stored in key specified by the given fnkey using the
 * arguments provided.
 */
kstatus_t
ki_aio_exec(int ktd, kapplet_t *app, void *cctx, kio_t **kio)
{
	return(e_exec_aio_generic(ktd, app, cctx, kio));
}


/**
 * kstatus_t
 * ki_exec(int ktd,  kapplet_t *app)
 *
 *  app		contains
 *
 * Exec the function stored in key specified by the given fnkey using the
 * arguments provided.
 */
kstatus_t
ki_exec(int ktd, kapplet_t *app)
{
	return(e_exec_generic(ktd, app));
}


/*
 * Helper functions
 */

/* Shorten this behemoth */
#define mapplet_init com__seagate__kinetic__proto__command__manage_applet__init

struct kresult_message
create_exec_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, kapplet_t *app)
{
	int i, j, len;
	kv_t *key;
	ProtobufCBinaryData *progkey = NULL;
	ProtobufCBinaryData outkey = {.data=NULL, .len=0,};
	kproto_kapplet_t proto_cmd_body;
	ProtobufCBinaryData command_bytes = {.data=NULL, .len=0,};

	mapplet_init(&proto_cmd_body);

	/* 
	 * consolidate and copy app fnkeys to the cmd body
	 * But first need an appropriately sized vector
	 */
	if (app->ka_fnkeycnt) {
		len = sizeof(ProtobufCBinaryData) * app->ka_fnkeycnt;
		progkey = (ProtobufCBinaryData *)KI_MALLOC(len);
		if (!progkey) {
			goto cb_ex;
		}
		memset(progkey, 0, len);
	}

	/* Now copy the keys, first the function keys */
	for (i=0; i<app->ka_fnkeycnt; i++) {
		if (!(key = app->ka_fnkey[i])) {
			goto cb_ex;
		}

		/* Get the length */
		for (progkey[i].len=0, j=0; j< key->kv_keycnt; j++)
			progkey[i].len += key->kv_key[j].kiov_len;

		/* Allocate the vector element */
		progkey[i].data = (uint8_t *)KI_MALLOC(progkey[i].len);
		if (!progkey[i].data) {
			goto cb_ex;
		};

		/* Copy key vector into it */
		for (len=0, j=0; j<key->kv_keycnt; j++) {
			strncpy((char *)&progkey[i].data[len],
				key->kv_key[j].kiov_base,
				key->kv_key[j].kiov_len);
			len += key->kv_key[j].kiov_len;
		}
	}

	/* Now the outkey if necessary */
	if (app->ka_outkey) {
		key = app->ka_outkey;
		
		/* Get the length */
		for (outkey.len=0, j=0; j<key->kv_keycnt; j++)
			outkey.len += key->kv_key[j].kiov_len;

		/* Allocate the outkey */
		outkey.data = (uint8_t *)KI_MALLOC(outkey.len);
		if (!outkey.data) {
			goto cb_ex;
		};

		/* Copy key vector into it */
		for (len=0, j=0; j<key->kv_keycnt; j++) {
			strncpy((char *)&outkey.data[len],
				key->kv_key[j].kiov_base,
				key->kv_key[j].kiov_len);
			len += key->kv_key[j].kiov_len;
		}
	}
		
	/* 
	 * Manage applet names in the proto are weird. 
	 * Type is actually the action/command to take. Currently only
	 * EXECUTE is supported.
	 * Lang is the type of binary to act on: Native, JAVA, LLVM IR, eBPF
	 * Target decoded kinetic PDU Body:
	 * ManageApplet:
	 *	ManageAppletType:EXECUTE
	 *	Language:VENDOR_DEPENDENT
	 *	programKey = string
	 *	(optional) programKey = string
	 *	AcknowledgeMode = 2
	 *	notifyOnCompletion = 1
	 *	(optional) outputKey = string
	 *	(optional) setValueInResponse = 1
	 */

	set_primitive_optional(&proto_cmd_body,
			       manageapplettype, CSMAT(EXECUTE));

	set_primitive_optional(&proto_cmd_body, lang, app->ka_fntype);

	proto_cmd_body.programkey = progkey;
	proto_cmd_body.n_programkey = app->ka_fnkeycnt;

	/* maxruntime unused */
	/* set_primitive_optional(&proto_cmd_body, maxruntime, 0); */

	/* processstatus unused */
	
	/* watchscope unused */
		
	proto_cmd_body.programparam   = app->ka_argv;
	proto_cmd_body.n_programparam = app->ka_argc;

	set_primitive_optional(&proto_cmd_body,
			       acknowledgemode, CSMAAM(ON_COMPLETION));

	/* notification unused */
	
	/* set_primitive_optional(&proto_cmd_body, notifyoncompletion, 1); */

	if (outkey.data) {
		set_primitive_optional(&proto_cmd_body, outputkey, outkey);
		set_primitive_optional(&proto_cmd_body, setvalueinresponse, 1);
	}
	
	/*
	 * Time to construct the command bytes to place into message
	 */
	command_bytes = create_command_bytes(cmd_hdr, (void *) &proto_cmd_body);

	/* 
	 * since the cmd_body now is in command bytes, 
	 * programkey and outkey can be freed
	 */
 cb_ex:
	for (i=0;i<app->ka_fnkeycnt; i++) {
		if (progkey[i].data)
			KI_FREE(progkey[i].data);
	}
	if (progkey)
		KI_FREE(progkey);

	if (outkey.data)
		KI_FREE(outkey.data);

	/* This is the exit for allocation errs above and cmd bytes failures */
	if (!command_bytes.data) {
		return (struct kresult_message) {
			.result_code = FAILURE,
			.result_message = NULL,
		};
	}

	// return the constructed exec message (or failure)
	return create_message(msg_hdr, command_bytes);
}

kstatus_t
extract_exec_response(struct kresult_message *resp_msg, kapplet_t *app)
{
	char      *dmsg;
	uint32_t   dmsglen;
	kstatus_t  krc;

	// commandbytes should exist
	kproto_msg_t *app_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!app_resp_msg->has_commandbytes) {
		debug_printf("extract_exec_response: no resp cmd");
		return (K_EINTERNAL);
	}

	// unpack command, and hang it on `app` to be destroyed at any time
	kproto_cmd_t *resp_cmd = unpack_kinetic_command(app_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_execkey: resp cmd unpack");
		return (K_EINTERNAL);
	}

	/* 
	 * extract the full status. On failure, skip to cleanup 
	 * Status:
	 *	StatusCode:EXECUTE_COMPLETE
	 *	statusMessage = Applet with keys: Hello finished executing!
	 *	detailedMessage = "0:0:Applet success:Args: /mnt/util/applets/4234354357\nHello World!"
	 */
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK				||
	    !resp_cmd->status->statusmessage	||
	    !resp_cmd->status->has_detailedmessage) {
		debug_printf("extract_exec: Invalid status message");
		goto extract_eex;
	}

	// ------------------------------
	// begin extraction of command data (just the output streams for `exec`)

	// Since everything seemed successful, let's pop this data on our cleaning stack
	krc = ki_addctx(app, resp_cmd, destroy_command);
	if (krc != K_OK) {
		debug_printf("extract_exec: destroy context\n");
		goto extract_eex;
	}

	/*
	 * The current detailed message layout is as follows:
	 * 	<rc>:<sig>:<mesg>:<stdout/stderr>
	 * 
	 * Decode/extract fields from formatted output message. mesg is tossed
	 */
	dmsg    = (char *) resp_cmd->status->detailedmessage.data;
	dmsglen = resp_cmd->status->detailedmessage.len;
	if (sscanf(dmsg, "%d:%d:", &app->ka_rc, &app->ka_sig) != 2) {
		debug_printf("extract_exec: bad detailed message format");
		goto extract_eex;
	}

	dmsg = strchr(dmsg, ':'); dmsg++;	/* forward past rc: */
	dmsg = strchr(dmsg, ':'); dmsg++;	/* forward past sig: */
	dmsg = strchr(dmsg, ':'); dmsg++;	/* toss mesg: */
	dmsglen -= (uint32_t)(dmsg - (char *)resp_cmd->status->detailedmessage.data);

	// set data in app structure, but caller will manage this data
	// TODO: if we want, these can just be pointers to protobuf data
	app->ka_msg       = resp_cmd->status->statusmessage;
	app->ka_stdout    = dmsg;
	app->ka_stdoutlen = dmsglen;

	if (app->ka_msg == NULL || app->ka_stdout == NULL) {
		debug_printf("extract_exec: could not allocate exec messages");
		goto extract_eex;
	}

	return krc;

 extract_eex:
	debug_printf("extract_exec: error exit\n");
	destroy_command(resp_cmd);

	// Just make sure we don't return an ok message
	if (krc == K_OK) { krc = K_EINTERNAL; }

	return krc;
}
