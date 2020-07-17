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
#include "kinetic_int.h"

/**
 * Internal prototypes
 */
ProtobufCBinaryData pack_cmd_get(kproto_cmdhdr_t *, kproto_getlog_t *);
void extract_to_command_header(kproto_cmdhdr_t *, kcmdhdr_t *);
void extract_to_command_body(kproto_getlog_t *, kgetlog_t *);
struct kresult_message create_get_message(kmsghdr_t *,
					     kcmdhdr_t *, kgetlog_t *);

/**
 * g_validate_kv(kv_t *kv, limit_t *lim)
 *
 *  kv		Always contains a key, but optionally may have a value
 *		However there must always value kiovec array of at least 
 * 		element and should always be initialized. if unused,
 *		as in a get, kv_val[0].kiov_base=NULL and kv_val[0].kiov_len=0
 *  lim 	Contains server limits	
 *
 * Validate that the user is passing a valid kv structure
 *
 */
static int
g_validate_kv(kv_t *kv, limit_t *lim)
{
	int i;
	size_t len;
	int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */

	
	/* Check the required key */
	if (!kv || !kv->kv_key || kv->kv_keycnt < 1) 
		return(-1);

	/* Total up the length across all vectors */
	for (len=0, i=0; i<kv->kv_keycnt; i++)
		len += kv->kv_key[i].kiov_len;

	if (len > lim->kl_keylen)
		return(-1);

	/* Check the value vectors */	
	if (!kv->kv_val || kv->kv_valcnt < 1) 
		return(-1);

	/* Total up the length across all vectors */
	for (len=0, i=0; i<kv->kv_valcnt; i++)
		len += kv->kv_val[i].kiov_len;

	if (len > lim->kl_vallen)
		return(-1);
	
	/* Check the version */
	if (kv->kv_vers &&
	    ((kv->kv_verslen < 1) || (kv->kv_verslen > lim->kl_verlen)))
		return(-1);

	/* Check the tag */
	if (kv->kv_tag &&
	    ((kv->kv_taglen < 1) || (kv->kv_taglen > lim->kl_taglen)))
		return(-1);

	/* Check the Data integrity Type */
	switch (kv->kvditype) {
	case 0: /* zero is unused by the protocol but is valid for gets */
	case KDI_SHA1:
	case KDI_SHA2:
	case KDI_SHA3:
	case KDI_CRC32C:
	case KDI_CRC64:
	case KDI_CRC32:
		break;
	default:
		return(-1);
		
	errno = 0;
	return(0);
}

/**
 * ki_get(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Get the value specified by the given key, tag, and version. 
 *
 */
kstatus_t
ki_get(int ktd, kv_t *kv)
{
	int rc, n;
	kstatus_t krc;
	struct kio *kio;
	struct ktli_config *cf;
	kpdu_t pdu = KP_INIT;
	kpdu_t *rpdu;
	kmsghdr_t msg_hdr;
	kcmdhdr_t cmd_hdr;
	ksession_t *ses;
	struct kresult_message kmreq, kmresp;

	rc = ktli_conf(ktd, *cf);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = K_REJECTED,
			.ks_message = "Bad session",
			.ks_detail  = "",
		};		
	}
	ses = ( ksession_t)cf->kcfg_pconf;
	
	/* Validate the passed in kv */
	rc = gl_validate_kv(kv, &ses->ks_l);
	if (rc < 0) {
		return (kstatus_t) {
			.ks_code    = errno,
			.ks_message = "Invalid KV",
			.ks_detail  = "",
		};
	}

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL;
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}
	memset(kio, 0, sizeof(struct kio));

	/* 
	 * Allocate kio vectors array of size 2
	 * One vector for the PDU and another for the full request message
	 */
	kio->kio_sendmsg.km_cnt = 2; 
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		return (kstatus_t) {
			.ks_code    = K_EINTERNAL;
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}

	/* Hang the PDU buffer */
	kio->kio_cmd = KMT_GET;
	kio->kio_sendmsg.km_msg[0].kiov_base = (void *) &pdu;
	kio->kio_sendmsg.km_msg[0].kiov_len = KP_LENGTH;

	/* pack the message */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hmac;

	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type      = KMT_GET;

	kmreq = create_get_message(&msg_hdr, &cmd_hdr, kv);
	if (kmreq.result_code == FAILURE) {
		goto gex2;
	}

	/* PAK: Error handling */
	// success: rc = 0; failure: rc = 1 (see enum kresult_code)
	rc = pack_kinetic_message(
		(kproto_msg_t *) &(kmreq.result_message),
		&(kio->kio_sendmsg.km_msg[1].kiov_base),
		&(kio->kio_sendmsg.km_msg[1].kiov_len)
	);

	/* Now that we have the msgSetup the PDU */
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[1].kiov_len;
	pdu.kp_vallen = 0;

	/* Send the request */
	ktli_send(ktd, kio);
	printf ("Sent Kio: %p\n", kio);

	/* Wait for the response */
	ktli_poll(ktd, 0);

	/* Receive the response */
	/* PAK: need error handling */
	rc = ktli_receive(ktd, kio);

	/* extract the return PDU */
	if (kio->kio_recvmsg.km_msg[0].kiov_len != sizeof(pdu_t)) {
		/* PAK: error handling -need to clean up Yikes! */
	}
	rpdu = kio->kio_recvmsg.km_msg[0].kiov_base;

	/* Does the PDU match what was given in the recvmsg */
	if (rpdu->kp_msglen + rpdu->kp_vallen !=
	    kio->kio_recvmsg.km_msg[1].kiov_len ) {
		/* PAK: error handling -need to clean up Yikes! */
	}

	/* Grab the value */
	kv->kv_val = kio->kio_recvmsg.km_msg[1].kiov_base + rpdu->kp_msglen;
	kv->kv_val = rpdu->kp_vallen;

	/* Now unpack the message */ 
	kmresp = unpack_get_resp(kio->kio_recvmsg.km_msg[1].kiov_base,
				    kio->kio_recvmsg.km_msg[1].kiov_len);

	if (kmresp->result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto gex2;
	}

	kstatus = extract_get(kmresp, kv);
	if (kstatus->ks_code != K_OK) {
		rc = -1;
		goto gex1;
	}

	rc = gl_validate_resp(glog);

	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}

	/* clean up */
 gex1:
	destroy_message(kmresp);
 gex2:
	destroy_message(kmreq);
	destroy_request(kio->kio_sendmsg.km_msg[1].kiov_base);

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_recvmsg.km_msg[0].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[1].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);
	KI_FREE(kio->kio_sendmsg.km_msg);
	KI_FREE(kio);

	// TODO: translae rc error code into kstatus_t
	// return (rc);
	return (kstatus_t) {
		.ks_code    = K_INVALID_SC,
		.ks_message = "Error sending getlog request: glex2",
		.ks_detail  = "",
	};
}
kstatus_t
ki_getnext(int ktd, kv_t *key, kv_t *next)
{


}

kstatus_t
ki_getprev(int ktd, kv_t *key, kv_t *prev)
{

}

kstatus_t
ki_getversion(int ktd, kv_t *key)
{



}

/*
 * Helper functions
 */
//TODO: test
ProtobufCBinaryData pack_cmd_getlog(kproto_cmdhdr_t *cmd_hdr, kproto_getlog_t *cmd_getlog) {
	// Structs to use
	kproto_cmd_t command_msg;
	kproto_body_t  command_body;

	// initialize the structs
	com__seagate__kinetic__proto__command__init(&command_msg);
	com__seagate__kinetic__proto__command__body__init(&command_body);

	// update the header for the GetLog Message Body
	cmd_hdr->messagetype = KMT_GETLOG;

	// stitch the Command together
	command_body.getlog = cmd_getlog;

	command_msg.header	= cmd_hdr;
	command_msg.body	= &command_body;

	return pack_kinetic_command(&command_msg);
}

void extract_to_command_body(kproto_getlog_t *proto_getlog, kgetlog_t *cmd_data) {
	com__seagate__kinetic__proto__command__get_log__init(proto_getlog);

	// Populate `types` field using `getlog_types_buffer` argument.
	proto_getlog->n_types = cmd_data->kgl_typecnt;
	proto_getlog->types	= (kgltype_t *) cmd_data->kgl_type;

	// Should device name have a length attribute?
	if (cmd_data->kgl_log.kdl_name != NULL) {
		kgetlog_device_info *getlog_msg_device = (kgetlog_device_info *) malloc(sizeof(kgetlog_device_info));
		com__seagate__kinetic__proto__command__get_log__device__init(getlog_msg_device);

		// TODO: see that kgl_log.len is used instead of computing strlen?
		getlog_msg_device->has_name = 1;
		getlog_msg_device->name		= (ProtobufCBinaryData) {
			// .len  =				cmd_data->kgl_log.len,
			.len  = (size_t)    strlen(cmd_data->kgl_log.kdl_name),
			.data = (uint8_t *) cmd_data->kgl_log.kdl_name,
		};

		proto_getlog->device = getlog_msg_device;
	}
}

void extract_to_command_header(kproto_cmdhdr_t *proto_cmdhdr, kcmdhdr_t *cmdhdr_data) {

	com__seagate__kinetic__proto__command__header__init(proto_cmdhdr);

	if (cmdhdr_data->kch_clustvers) {
		proto_cmdhdr->has_clusterversion = 1;
		proto_cmdhdr->clusterversion     = cmdhdr_data->kch_clustvers;
	}

	if (cmdhdr_data->kch_connid) {
		proto_cmdhdr->connectionid	   = cmdhdr_data->kch_connid;
		proto_cmdhdr->has_connectionid = 1;
	}

	if (cmdhdr_data->kch_timeout) {
		proto_cmdhdr->timeout	  = cmdhdr_data->kch_timeout;
		proto_cmdhdr->has_timeout = 1;
	}

	if (cmdhdr_data->kch_pri) {
		proto_cmdhdr->priority	   = cmdhdr_data->kch_pri;
		proto_cmdhdr->has_priority = 1;
	}

	if (cmdhdr_data->kch_quanta) {
		proto_cmdhdr->timequanta	 = cmdhdr_data->kch_quanta;
		proto_cmdhdr->has_timequanta = 1;
	}

	if (cmdhdr_data->kch_batid) {
		proto_cmdhdr->batchid	  = cmdhdr_data->kch_batid;
		proto_cmdhdr->has_batchid = 1;
	}
}


/*
 * Externally accessible functions
 */
// TODO: test
struct kresult_message create_getlog_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, kgetlog_t *cmd_body) {

	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_getlog_t proto_cmd_body;

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);
	extract_to_command_body(&proto_cmd_body, cmd_body);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = pack_cmd_getlog(&proto_cmd_header, &proto_cmd_body);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}
