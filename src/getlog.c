/**
 * Copyright 2013-2015 Seagate Technology LLC.
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
#include "getlog.h"

/**
 * Internal prototypes
 */
ProtobufCBinaryData pack_cmd_getlog(kproto_cmdhdr_t *, kproto_getlog_t *);
void extract_to_command_header(kproto_cmdhdr_t *, kcmdhdr_t *);
void extract_to_command_body(kproto_getlog_t *, kgetlog_t *);
struct kresult_message create_getlog_message(kmsghdr_t *,
					     kcmdhdr_t *, kgetlog_t *);

/**
 * gl_validate_req(kgetlog_t *glrq)
 *
 *  glrq 	contains the user passed in glog
 *
 * Validate that the user is asking for valid information. The type array
 * in the glrq may only have valid log types and they may not be repeated.
 *
 */
static int
gl_validate_req(kgetlog_t *glrq)
{
	int i;
	int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */

	/* Check the the requested types */
	if (!glrq || !glrq->kgl_type || !glrq->kgl_typecnt)
		return(-1);

	/*
	 * PAK: what if LOG and MESSAGES are set? does log use messages
	 *  for its data?
	 */
	/* track how many times a type is in the array */
	util = temp = cap = conf = stat = mesg = lim = log = 0;
	for (i=0; i<glrq->kgl_typecnt; i++) {
		switch (glrq->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
			util++; break;
		case KGLT_TEMPERATURES:
			temp++; break;
		case KGLT_CAPACITIES:
			cap++; break;
		case KGLT_CONFIGURATION:
			conf++; break;
		case KGLT_STATISTICS:
			stat++; break;
		case KGLT_MESSAGES:
			mesg++; break;
		case KGLT_LIMITS:
			lim++; break;
		case KGLT_LOG:
			log++; break;
		default:
			return(-1);
		}
	}

	/* if a type is repeated, fail */
	if (util>1 || temp>1 || cap>1 || conf>1 ||
	    stat>1 || mesg>1 || lim>1 || log>1) {
			return(-1);
	}

	/* If log expect a logname, if not then there shouldn't be a name */
	if (log) {
		if (glrq->kgl_log.kdl_name)
			if (!glrq->kgl_log.kdl_len ||
			    (glrq->kgl_log.kdl_len > 1024) {
				return(-1);
			}
		} else {
			/* Log requested but no name fail with default err */
			return(-1);
		}
	} else if (glrq->kgl_log.kdl_name) {
		/* Shouldn't have a name without a LOG type */
		return (-1);
	} /* no log and no logname, thats good */

	/* make sure all other ptrs and cnts are NULL and 0 */
	if (glrq->kgl_util	|| glrq->kgl_utilcnt	||
	    glrq->kgl_temp	|| glrq->kgl_tempcnt	||
	    glrq->kgl_stat	|| glrq->kgl_statcnt	||
	    glrq->kgl_msgs	|| glrq->kgl_msgscnt) {
		return(-1);
	}

	errno = 0;
	return(0);
}

/**
 * gl_validate_resp(kgetlog_t *glrq, *glrsp)
 *
 *  glrq 	contains the original user passed in glog
 *  glrsp	contains the server returned glog
 *
 * Validate that the server answered the request and the glog structure is
 * correct.
 */
static int
gl_validate_resp(kgetlog_t *glrq, *glrsp)
{
	int i, j;
	int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */

	/*
	 * Check the reqs and resp type exist types and
	 * that cnts should be the same
	 */
	if (!glrq || glrsp  ||
	    !glrq->kgl_type || !glrsp->kgl_type ||
	    (glrq->kgl_typecnt != glrq->kgl_typecnt)) {
		return(-1);
	}

	/*
	 * build up vars that represent requested types, this will
	 * allow correct accounting by decrementing them as we find
	 * them in the responses below. The req was hopefully already
	 * validated before receiving a response this validation garantees
	 * unique requested types.
	 */
	util = temp = cap = conf = stat = mesg = lim = log = 0;
	for (i=0; i<glrq->kgl_typecnt; i++) {
		switch (glrq->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
			util++; break;
		case KGLT_TEMPERATURES:
			temp++; break;
		case KGLT_CAPACITIES:
			cap++; break;
		case KGLT_CONFIGURATION:
			conf++; break;
		case KGLT_STATISTICS:
			stat++; break;
		case KGLT_MESSAGES:
			mesg++; break;
		case KGLT_LIMITS:
			lim++; break;
		case KGLT_LOG:
			log++; break;
		default:
			return(-1);
		}
	}

	for (i=0; i<glrsp->kgl_typecnt; i++) {
		/* match this response type to a req type */
		for (j=0; j<glrq->kgl_typecnt; j++) {
			if (glrsp->kgl_type[i] == glrq->kgl_type[i]) {
				break;
			}
		}

		/*
		 * if the 'for' above is exhausted then no match,
		 * the resp has an answer that was not requested
		 */
		if (j == glrq->kgl_typecnt) {
			return(-1);
		}

		/* got a match */
		switch (glrsp->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
			util--;  /* dec to account for the req */

			/* if an util array is provided */
			if (!glrsp->kgl_util || !glrsp->kgl_utilcnt)
				return(-1);
			break;
		case KGLT_TEMPERATURES:
			temp--;  /* dec to account for the req */

			/* if an temp array is provided */
			if (!glrsp->kgl_temp || !glrsp->kgl_tempcnt)
				return(-1);
			break;
		case KGLT_CAPACITIES:
			cap++;  /* dec to account for the req */

			/* cap built into get log, no way to validate */
			break;
		case KGLT_CONFIGURATION:
			conf--;  /* dec to account for the req */

			/* conf built into get log, no way to validate */
			break;
		case KGLT_STATISTICS:
			stat--;  /* dec to account for the req */

			/* if an stat array is provided */
			if (!glrsp->kgl_stat || !glrsp->kgl_statcnt)
				return(-1);
			break;
		case KGLT_MESSAGES:
			mesg--;  /* dec to account for the req */

			/* if an msgs buf is provided */
			if (!glrsp->kgl_msgs || !glrsp->kgl_msgscnt)
				return(-1);
			break;
		case KGLT_LIMITS:
			lim--;  /* dec to account for the req */

			/* limits built into get log, no way to validate */
			break;
		case KGLT_LOG:
			log--;  /* dec to account for the req */
			/* if an msgs buf is provided */
			if (!glrsp->kgl_msgs || !glrsp->kgl_msgscnt)
				return(-1);
			break;
		default:
			/* Bad type */
			return(-1);
		}
	}

	/* if every req type was found in the resp all these should be 0 */
	if (util || temp || cap || conf || stat || mesg || lim || log) {
			return(-1);
	}

	errno = 0;
	return(0);
}

kstatus_t
ki_getlog(int ktd, kgetlog_t *glog)
{
	int rc, n;
	kstatus_t krc;
	struct kio *kio;
	kpdu_t pdu = KP_INIT;
	kmsghdr_t msg_hdr;
	kcmdhdr_t cmd_hdr;
	kgetlog_t *glog2;
	struct kresult_message kmreq, kmresp;

	/* Validate the passed in glog */
	rc = gl_validate_req(glog);
	if (rc < 0) {
		/* errno set by validate */

		// TODO: set ks_detail
		// .ks_detail  = sprintf("gl_validate_req returned error: %d\n", rc
		return (kstatus_t) {
			.ks_code    = K_INVALID_SC,
			.ks_message = "Invalid request",
			.ks_detail  = "",
		};
	}

	/* create the kio structure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		errno = ENOMEM;

		// TODO: set ks_detail
		return (kstatus_t) {
			.ks_code    = K_INVALID_SC,
			.ks_message = "Unable to allocate memory for request",
			.ks_detail  = "",
		};
	}
	memset(kio, 0, sizeof(struct kio));

	/* Alocate the kio vectors */
	kio->kio_sendmsg.km_cnt = 2; /* PDU and protobuf */
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		errno = ENOMEM;

		// TODO: set ks_detail
		return (kstatus_t) {
			.ks_code    = K_INVALID_SC,
			.ks_message = "Error sending request",
			.ks_detail  = "",
		};
	}

	/* Hang the PDU buffer */
	kio->kio_cmd = KMT_GETLOG;
	kio->kio_sendmsg.km_msg[0].kiov_base = (void *) &pdu;
	kio->kio_sendmsg.km_msg[0].kiov_len = KP_LENGTH;

	/* pack the message */
	/* PAK: fill out kmsghdr and kcmdhdr */
	/* PAK: Need to save conn config on session, for use here */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = 1;
	msg_hdr.kmh_hmac  = "abcdefgh";

	memset((void *) &cmd_hdr, 0, sizeof(cmd_hdr));
	cmd_hdr.kch_clustvers = 0;
	cmd_hdr.kch_connid    = 0;
	cmd_hdr.kch_type      = KMT_GETLOG;

	kmreq = create_getlog_message(&msg_hdr, &cmd_hdr, glog);
	if (kmreq.result_code == FAILURE) {
		goto glex2;
	}

	/* PAK: Error handling */
	// success: rc = 0; failure: rc = 1 (see enum kresult_code)
	rc = pack_kinetic_message(
		(kproto_msg_t *) &(kmreq.result_message),
		&(kio->kio_sendmsg.km_msg[1].kiov_base),
		&(kio->kio_sendmsg.km_msg[1].kiov_len)
	);

	
	/* Setup the PDU */
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

	kmresp = unpack_getlog_resp(kio->kio_sendmsg.km_msg[1].kiov_base,
				    kio->kio_sendmsg.km_msg[1].kiov_len);

	if (kmresp->result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto glex2;
	}

	kstatus = extract_getlog(kmresp, &glog2);
	if (kstatus->ks_code != K_OK) {
		rc = -1;
		goto glex1;
	}

	rc = gl_validate_resp(glog, glog2);

	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}

	/* PAK: need to copy glog2 into glog and freee up glog2 */
	
	/* clean up */
 glex1:
	destroy_message(kmresp);
 glex2:
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
