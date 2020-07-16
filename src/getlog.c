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
struct kresult_message create_getlog_message(kmsghdr_t *, kcmdhdr_t *, kgetlog_t *);
kstatus_t extract_getlog(struct kresult_message *getlog_response_msg, kgetlog_t *getlog_data);

static int
gl_validate_req(kgetlog_t *glog)
{
	int i, log;

	errno = EINVAL;  /* assume we will find a problem */

	/* Check the the requested types */
	if (!glog->kgl_type || !glog->kgl_typecnt)
		return(-1);
	log = 0;
	for (i=0; i<glog->kgl_typecnt; i++) {
		switch (glog->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
		case KGLT_TEMPERATURES:
		case KGLT_CAPACITIES:
		case KGLT_CONFIGURATION:
		case KGLT_STATISTICS:
		case KGLT_MESSAGES:
		case KGLT_LIMITS:
			/* a good type */
			break;

		case KGLT_LOG:
			/* a good type - LOG has special error checking */
			log = 1;
			break;

		default:
			return(-1);
		}
	}

	/* If log expect a logname, if not then there shouldn't be a name */
	if (log) {
		if (glog->kgl_log.kdl_name) {
			// TODO: recommend using an actual length, because it needs to be passed anyways.
			// (see glog->kgl_log.len in getlog.h)
			int n = strlen(glog->kgl_log.kdl_name);
			if (!n || n > 1024) {
				errno = ERANGE;
				return(-1);
			}
		} else {
			/* Log requested but no name fail with default err */
			return(-1);
		}
	} else if (glog->kgl_log.kdl_name) {
		/* Shouldn't have a name without a LOG type */
		return (-1);
	}

	/* make sure all other ptrs and cnts are NULL and 0 */
	if (   glog->kgl_util
		|| glog->kgl_utilcnt
		|| glog->kgl_temp
		|| glog->kgl_tempcnt
		|| glog->kgl_stat
		|| glog->kgl_statcnt
		|| glog->kgl_msgs
		|| glog->kgl_msgscnt) {
		return(-1);
	}

	errno = 0;
	return(0);
}

static int
gl_validate_resp(kgetlog_t *glog)
{

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

	kmresp = unpack_kinetic_message(kio->kio_sendmsg.km_msg[1].kiov_base,
									kio->kio_sendmsg.km_msg[1].kiov_len);

	if (kmresp.result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto glex2;
	}

	kstatus_t command_status = extract_getlog(&kmresp, glog);
	if (command_status->ks_code != K_OK) {
		rc = -1;
		goto glex1;
	}

	rc = gl_validate_resp(glog);

	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}

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

kstatus_t extract_getlog(struct kresult_message *response_msg, kgetlog_t *getlog_data) {
	kproto_msg_t *getlog_response_msg = (kproto_msg_t *) response_msg->result_message;

	// commandbytes should exist, but we should probably be thorough
	if (!getlog_response_msg->has_commandbytes) {
		return (kstatus_t) {
			.ks_code    = K_INVALID_SC,
			.ks_message = "Response message has no command bytes",
			.ks_detail  = NULL
		};
	}

	// unpack the command bytes into a command structure
	kproto_cmd_t *cmd_response = unpack_kinetic_command(getlog_response_msg->commandbytes);

	// populate getlog_data from command body
	kproto_getlog_t *getlog_response = cmd_response->body->getlog;

	// TODO: need to walk each utilization
	//getlog_data->kgl_util = *(getlog_response->utilizations);

	// propagate the response status to the caller
	kproto_status_t *response_status = cmd_response->status;
	return (kstatus_t) {
		.ks_code    = response_status->has_code ? response_status->code : K_INVALID_SC,
		.ks_message = response_status->statusmessage,
		.ks_detail  = response_status->has_detailedmessage ? response_status->detailedmessage : NULL
	}
}
