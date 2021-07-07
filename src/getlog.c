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
 * getlog_ctx and destroy_getlog_ctx are for managing a kgetlog_t structure
 * and its associated protobuf structure. Expected usage of these is that a
 * `struct getlog_ctx` will be allocated and tracked by a KTB context (`ktb_ctx`).
 * When the getlog_ctx is ready to be released (or on failure to add it to
 * a ktb, `destroy_getlog_ctx` is invoked and the allocated getlog_ctx is provided.
 * The `destroy_getlog_ctx` will free each pointer (the protobuf struct and
 * pointers in the getlog struct that point to the protobuf struct), then free
 * the allocated `getlog_ctx`, itself.
 */
typedef struct getlog_ctx {
	kproto_cmd_t *response_cmd;
	kgetlog_t    *getlog_data;
} getlog_ctx_t;

void destroy_getlog_ctx(void *ctx_ptr) {
	// If we have nothing to do; return.
	if (!ctx_ptr) { return; }

	getlog_ctx_t *ctx_data    = (getlog_ctx_t *) ctx_ptr;
	kgetlog_t    *getlog_data = ctx_data->getlog_data;

	// First, release the allocated memory for the protobuf message data
	if (ctx_data->response_cmd) { destroy_command(ctx_data->response_cmd); }

	// Second, release each array of pointers that now point to free'd memory
	if (getlog_data) {
		if (getlog_data->kgl_util) { KI_FREE(getlog_data->kgl_util); }
		if (getlog_data->kgl_temp) { KI_FREE(getlog_data->kgl_temp); }
		if (getlog_data->kgl_stat) { KI_FREE(getlog_data->kgl_stat); }

		if (getlog_data->kgl_conf.kcf_serial) {
			KI_FREE(getlog_data->kgl_conf.kcf_serial);
		}

		if (getlog_data->kgl_conf.kcf_wwn) {
			KI_FREE(getlog_data->kgl_conf.kcf_wwn);
		}

		if (getlog_data->kgl_conf.kcf_interfaces) {
			KI_FREE(getlog_data->kgl_conf.kcf_interfaces);
		}
	}

	// Finally, free the ctx pointer, itself
	KI_FREE(ctx_ptr);
}


/**
 * ki_getlog(int ktd, kgetlog_t *glog)
 *
 *  ktd 	contains the an opened and connected KTLI session descriptor
 *  glog	contains the requested glog types and is filled in with
 *		the getlog data from the server
 *
 * Issue and return a kinetic getlog request
 */
kstatus_t
ki_getlog(int ktd, kgetlog_t *glog)
{
	int rc, n;
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
	if (rc < 0) {
		debug_printf("getlog: ktli config\n");
		return(K_EBADSESS);
	}

	ses = (ksession_t *)cf->kcfg_pconf;

	/* Validate the passed in glog; this function sets errno */
	rc = ki_validate_glog(glog);
	if (rc < 0) {
		debug_printf("getlog: validation\n");
		return(K_EINVAL);
	}
	
	/* create the kio structure; first malloc, so we return on failure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		debug_printf("getlog: kio alloc\n");
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
	 * used later on to calculate the actual HMAC which will then be hung
	 * of the kmh_hmac field.  A reference is made to the kcfg_hkey ptr 
	 * in the kmreq. This reference needs to be removed before freeing 
	 * kmreq. See below at glex_req:
	 */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KAT_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type      = KMT_GETLOG;

	kmreq = create_getlog_message(&msg_hdr, &cmd_hdr, glog);
	if (kmreq.result_code == FAILURE) {
		debug_printf("getlog: request message create\n");
		krc = K_EINTERNAL;
		goto glex_kio;
	}

	/* Setup the KIO */
	kio->kio_cmd            = KMT_GETLOG;
	kio->kio_flags		= KIOF_INIT;
	KIOF_SET(kio, KIOF_REQRESP);		/* Normal RPC */

	/* 
	 * Allocate kio vectors array. Element 0 is for the PDU, element 1
	 * is for the protobuf message. There is no value.
	 * See message.h for more details.
	 */
	kio->kio_sendmsg.km_cnt = KM_CNT_NOVAL;
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		debug_printf("getlog: sendmesg alloc\n");
		krc = K_ENOMEM;
		goto glex_req;
	}

	/* Hang the Packed PDU buffer, packing occurs later */
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_base = (void *) ppdu;
	kio->kio_sendmsg.km_msg[KIOV_PDU].kiov_len  = KP_PLENGTH;

	/* pack the message and hang it on the kio */
	/* PAK: Error handling */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	enum kresult_code pack_result = pack_kinetic_message(
		(kproto_msg_t *) kmreq.result_message,
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base),
		&(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_len)
	);

	if (pack_result == FAILURE) {
		debug_printf("getlog: sendmesg msg pack\n");
		krc = K_EINTERNAL;
		goto glex_sendmsg;
	}
	
	/* Now that the message length is known, setup the PDU and pack it */
	pdu.kp_magic  = KP_MAGIC;
	pdu.kp_msglen = kio->kio_sendmsg.km_msg[1].kiov_len;
	pdu.kp_vallen = 0;
	PACK_PDU(&pdu, ppdu);
	debug_printf("getlog: PDU(x%2x, %d, %d)\n",
	       pdu.kp_magic, pdu.kp_msglen ,pdu.kp_vallen);

	/* Send the request */
	ktli_send(ktd, kio);
	debug_printf("Sent Kio: %p\n", kio);

	/* Wait for the response */
	do {
		/* wait for something to come in */
		ktli_poll(ktd, 0);

		/* Check to see if it our response */
		rc = ktli_receive(ktd, kio);
		if (rc < 0) {
			/* Not our response, so try again */
			if (errno == ENOENT) { continue; }
			else {
				/* PAK: need to exit, receive failed */
				debug_printf("getlog: kio receive failed\n");
				krc = K_EINTERNAL;
				goto glex_sendmsg;
			}
		}

		/* Got our response */
		else { break; }
	} while (1);

	/*
	 * Can for several reasons, i.e. TIMEOUT, FAILED, DRAINING, get a KIO
	 * that is really in an error state, in those cases clean up the KIO
	 * and go.
	 */
	if (kio->kio_state == KIO_TIMEDOUT) {
		debug_printf("getlog: kio timed out\n");
		krc = K_ETIMEDOUT;
		goto glex_recvmsg;
	} else 	if (kio->kio_state == KIO_FAILED) {
		debug_printf("getlog: kio failed\n");
		krc = K_ENOMSG;
		goto glex_recvmsg;
	}

	// Begin: Added PDU checking code based on src/batch.c
	/* extract the return PDU */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_PDU];
	if (kiov->kiov_len != KP_PLENGTH) {
		debug_printf("getlog: PDU bad length\n");
		krc = K_EINTERNAL;
		goto glex_recvmsg;
	}
	UNPACK_PDU(&rpdu, ((uint8_t *)(kiov->kiov_base)));

	/* Does the PDU match what was given in the recvmsg */
	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	if (rpdu.kp_msglen + rpdu.kp_vallen != kiov->kiov_len) {
		debug_printf("getlog: PDU decode\n");
		krc = K_EINTERNAL;
		goto glex_recvmsg;
	}

	// End: Added PDU checking code based on src/batch.c

	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		debug_printf("getlog: msg unpack\n");
		krc = K_EINTERNAL;
		goto glex_resp;
	}

	// (#50) We no longer need a copy, because extract_getlog will rollback
	// glog data on failure
	krc = extract_getlog(&kmresp, glog);

	/* clean up */
 glex_resp:
	destroy_message(kmresp.result_message);

 glex_recvmsg:
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_PDU].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);

 glex_sendmsg:
	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_sendmsg.km_msg[KIOV_MSG].kiov_base);
	KI_FREE(kio->kio_sendmsg.km_msg);

 glex_req:
	/*
	 * Tad bit hacky. Need to remove a reference to kcfg_hkey that 
	 * was made in kmreq before calling destroy.
	 * See 'Setup msg_hdr' comment above for details.
	 */
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.data = NULL;
	((kproto_msg_t *) kmreq.result_message)->hmacauth->hmac.len = 0;
	
	destroy_message(kmreq.result_message);

glex_kio:
	KI_FREE(kio);

	return (krc);
}

/*
 * Helper functions
 */
void extract_to_command_body(kproto_getlog_t *proto_getlog, kgetlog_t *cmd_data) {
	com__seagate__kinetic__proto__command__get_log__init(proto_getlog);

	// Populate `types` field using `getlog_types_buffer` argument.
	proto_getlog->n_types = cmd_data->kgl_typecnt;
	proto_getlog->types	= (kgltype_t *) cmd_data->kgl_type;

	// Should device name have a length attribute?
	if (cmd_data->kgl_log.kdl_name != NULL) {
		kgetlog_device_info *getlog_msg_device = (kgetlog_device_info *) KI_MALLOC(sizeof(kgetlog_device_info));
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


/*
 * Externally accessible functions
 */
// TODO: test
struct kresult_message create_getlog_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, kgetlog_t *cmd_body) {

	// declare protobuf structs on stack
	kproto_getlog_t proto_cmd_body;

	// populate protobuf structs
	extract_to_command_body(&proto_cmd_body, cmd_body);

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(cmd_hdr, &proto_cmd_body);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

int extract_utilizations(kgetlog_t *getlog_data, size_t n_utils, kproto_utilization_t **util_data) {
	getlog_data->kgl_utilcnt = n_utils;

	if (!n_utils) {
		getlog_data->kgl_util = NULL;
		return 0;
	}

	getlog_data->kgl_util = (kutilization_t *) KI_MALLOC(sizeof(kutilization_t) * n_utils);
	if (getlog_data->kgl_util == NULL) { return -1; }

	for (int ndx = 0; ndx < n_utils; ndx++) {
		getlog_data->kgl_util[ndx].ku_name = util_data[ndx]->name;
		extract_primitive_optional(getlog_data->kgl_util[ndx].ku_value, util_data[ndx], value);
	}

	return 0;
}

int extract_temperatures(kgetlog_t *getlog_data, size_t n_temps, kproto_temperature_t **temp_data) {
	getlog_data->kgl_tempcnt = n_temps;

	// no temperatures to extract
	if (!n_temps) {
		getlog_data->kgl_temp = NULL;
		return 0;
	}

	getlog_data->kgl_temp = (ktemperature_t *) KI_MALLOC(sizeof(ktemperature_t) * n_temps);
	if (getlog_data->kgl_temp == NULL) { return -1; }

	for (int ndx = 0; ndx < n_temps; ndx++) {
		getlog_data->kgl_temp[ndx].kt_name = temp_data[ndx]->name;

		// use convenience macros for optional fields
		extract_primitive_optional(getlog_data->kgl_temp[ndx].kt_cur   , temp_data[ndx], current);
		extract_primitive_optional(getlog_data->kgl_temp[ndx].kt_min   , temp_data[ndx], minimum);
		extract_primitive_optional(getlog_data->kgl_temp[ndx].kt_max   , temp_data[ndx], maximum);
		extract_primitive_optional(getlog_data->kgl_temp[ndx].kt_target, temp_data[ndx], target);
	}

	return 0;
}

int extract_statistics(kgetlog_t *getlog_data, size_t n_stats, kproto_statistics_t **stat_data) {
	getlog_data->kgl_statcnt = n_stats;

	// no stats to extract
	if (!n_stats || !stat_data) {
		getlog_data->kgl_stat = NULL;
		return 0;
	}

	getlog_data->kgl_stat = (kstatistics_t *) KI_MALLOC(sizeof(kstatistics_t) * n_stats);
	if (getlog_data->kgl_stat == NULL) { return -1; }

	for (int ndx = 0; ndx < n_stats; ndx++) {
		// use convenience macros for optional fields
		extract_primitive_optional(getlog_data->kgl_stat[ndx].ks_mtype     , stat_data[ndx], messagetype);
		extract_primitive_optional(getlog_data->kgl_stat[ndx].ks_cnt       , stat_data[ndx], count);
		extract_primitive_optional(getlog_data->kgl_stat[ndx].ks_bytes     , stat_data[ndx], bytes);
		extract_primitive_optional(getlog_data->kgl_stat[ndx].ks_maxlatency, stat_data[ndx], maxlatency);
	}

	return 0;
}

int extract_interfaces(kinterface_t **getlog_if_data, size_t n_ifs, kproto_interface_t **if_data) {
	if (!n_ifs) {
		*getlog_if_data = NULL;
		return 0;
	}

	*getlog_if_data = (kinterface_t *) KI_MALLOC(sizeof(kinterface_t) * n_ifs);
	if (*getlog_if_data == NULL) { return -1; }

	for (int if_ndx = 0; if_ndx < n_ifs; if_ndx++) {
		(*getlog_if_data)[if_ndx].ki_name = if_data[if_ndx]->name;

		if (if_data[if_ndx]->has_mac) {
			(*getlog_if_data)[if_ndx].ki_mac = helper_bytes_to_str(if_data[if_ndx]->mac);
		}
		if (if_data[if_ndx]->has_ipv4address) {
			(*getlog_if_data)[if_ndx].ki_ipv4 = helper_bytes_to_str(if_data[if_ndx]->ipv4address);
		}
		if (if_data[if_ndx]->has_ipv6address) {
			(*getlog_if_data)[if_ndx].ki_ipv6 = helper_bytes_to_str(if_data[if_ndx]->ipv6address);
		}
	}

	return 0;
}

int extract_configuration(kgetlog_t *getlog_data, kproto_configuration_t *config) {
	// nothing to extract
	if (config == NULL) {
		// NOTE: this may be repetitive
		memset(&(getlog_data->kgl_conf), 0, sizeof(kconfiguration_t));
		return 0;
	}

	getlog_data->kgl_conf.kcf_vendor = config->vendor;
	getlog_data->kgl_conf.kcf_model  = config->model;

	// NOTE: we may want to have a different representation of `bytes` fields
	// NOTE: serial and wwn are `free`d in `destroy_getlog_ctx`
	if (config->has_serialnumber) {
		getlog_data->kgl_conf.kcf_serial = helper_bytes_to_str(config->serialnumber);
	}

	if (config->has_worldwidename) {
		getlog_data->kgl_conf.kcf_wwn = helper_bytes_to_str(config->worldwidename);
	}

	getlog_data->kgl_conf.kcf_version       = config->version;
	getlog_data->kgl_conf.kcf_compdate      = config->compilationdate;
	getlog_data->kgl_conf.kcf_srchash       = config->sourcehash;
	getlog_data->kgl_conf.kcf_proto         = config->protocolversion;
	getlog_data->kgl_conf.kcf_protocompdate = config->protocolcompilationdate;
	getlog_data->kgl_conf.kcf_protosrchash  = config->protocolsourcehash;

	extract_primitive_optional(getlog_data->kgl_conf.kcf_port   , config, port);
	extract_primitive_optional(getlog_data->kgl_conf.kcf_tlsport, config, tlsport);
	extract_primitive_optional(getlog_data->kgl_conf.kcf_power  , config, currentpowerlevel);

	// interfaces
	int extract_result = extract_interfaces(
		&(getlog_data->kgl_conf.kcf_interfaces),
		config->n_interface,
		config->interface
	);
	if (extract_result < 0) { return -1; }

	getlog_data->kgl_conf.kcf_interfacescnt = config->n_interface;

	return 0;
}

void extract_limits(kgetlog_t *getlog_data, kproto_limits_t *limits) {
	// NOTE: This is probably repetitive
	memset(&(getlog_data->kgl_limits), 0, sizeof(klimits_t));

	if (limits) {
		extract_primitive_optional(getlog_data->kgl_limits.kl_keylen     , limits, maxkeysize                 );
		extract_primitive_optional(getlog_data->kgl_limits.kl_vallen     , limits, maxvaluesize               );
		extract_primitive_optional(getlog_data->kgl_limits.kl_verlen     , limits, maxversionsize             );
		extract_primitive_optional(getlog_data->kgl_limits.kl_disumlen   , limits, maxtagsize                 );
		extract_primitive_optional(getlog_data->kgl_limits.kl_msglen     , limits, maxmessagesize             );
		extract_primitive_optional(getlog_data->kgl_limits.kl_pinlen     , limits, maxpinsize                 );
		extract_primitive_optional(getlog_data->kgl_limits.kl_batlen     , limits, maxbatchsize               );
		extract_primitive_optional(getlog_data->kgl_limits.kl_pendrdcnt  , limits, maxoutstandingreadrequests );
		extract_primitive_optional(getlog_data->kgl_limits.kl_pendwrcnt  , limits, maxoutstandingwriterequests);
		extract_primitive_optional(getlog_data->kgl_limits.kl_conncnt    , limits, maxconnections             );
		extract_primitive_optional(getlog_data->kgl_limits.kl_idcnt      , limits, maxidentitycount           );
		extract_primitive_optional(getlog_data->kgl_limits.kl_rangekeycnt, limits, maxkeyrangecount           );
		extract_primitive_optional(getlog_data->kgl_limits.kl_batopscnt  , limits, maxoperationcountperbatch  );
		extract_primitive_optional(getlog_data->kgl_limits.kl_batdelcnt  , limits, maxdeletesperbatch         );
		extract_primitive_optional(getlog_data->kgl_limits.kl_devbatcnt  , limits, maxbatchcountperdevice     );
	}
}


kstatus_t
extract_getlog(struct kresult_message *resp_msg, kgetlog_t *getlog_data)
{
	int           needs_rollback = 0;
	kstatus_t     krc            = K_EINTERNAL;
	kproto_msg_t *getlog_resp_msg;
	kgetlog_t     rollback_data;

	// commandbytes should exist
	getlog_resp_msg = (kproto_msg_t *) resp_msg->result_message;
	if (!getlog_resp_msg->has_commandbytes) {
		debug_printf("extract_getlog: no resp cmd\n");
		return krc;
	}

	kproto_cmd_t *resp_cmd = unpack_kinetic_command(getlog_resp_msg->commandbytes);
	if (!resp_cmd) {
		debug_printf("extract_getlog: resp cmd unpack\n");
		return krc;
	}

	// try allocating this first to simplify the error modes
	// on error, only the unpacked command has been allocated so far.
	getlog_ctx_t *ctx_pair = (getlog_ctx_t *) KI_MALLOC(sizeof(getlog_ctx_t));
	if (!ctx_pair) {
		destroy_command(resp_cmd);
		debug_printf("extract_getlog: unable to allocate context\n");
		return krc;
	}

	// make sure we don't erroneously try to cleanup garbage pointers
	memset(ctx_pair, 0, sizeof(getlog_ctx_t));

	// extract the status. On failure, skip to cleanup
	krc = extract_cmdstatus_code(resp_cmd);
	if (krc != K_OK) {
		debug_printf("extract_getlog: status\n");
		goto extract_glex;
	}

	// check if there's command data to parse, otherwise cleanup and exit
	if (!resp_cmd->body || !resp_cmd->body->getlog) {
		debug_printf("extract_getlog: command missing body or getlog\n");
		goto extract_glex;
	}

	// Since everything seemed successful, let's pop this data on our cleaning stack
	// NOTE: ctx_pair is free-d by destroy_getlog_ctx
	*ctx_pair = (getlog_ctx_t) {
		 .response_cmd = resp_cmd
		,.getlog_data  = getlog_data
	};

	krc = ki_addctx(getlog_data, (void *) ctx_pair, destroy_getlog_ctx);
	if (krc != K_OK) {
		debug_printf("extract_getlog: failed to add context\n");
		goto extract_glex;
	}

	// ------------------------------
	// begin extraction of command data

	// 0 is success, < 0 is failure. Use this for all the extract functions
	int extract_result;

	// save original getlog data somewhere so that we can rollback on failure
	memcpy(&rollback_data, getlog_data, sizeof(kgetlog_t));
	needs_rollback = 1;
	kproto_getlog_t *resp = resp_cmd->body->getlog;

	getlog_data->kgl_typecnt = resp->n_types;
	getlog_data->kgl_type    = resp->types;

	// repeated fields
	extract_result = extract_utilizations(
		getlog_data,
		resp->n_utilizations,
		resp->utilizations
	);
	if (extract_result < 0) { goto extract_glex; }

	extract_result = extract_temperatures(
		getlog_data,
		resp->n_temperatures,
		resp->temperatures
	);
	if (extract_result < 0) { goto extract_glex; }

	extract_result = extract_statistics(
		getlog_data,
		resp->n_statistics,
		resp->statistics
	);
	if (extract_result < 0) { goto extract_glex; }

	// then optional fields (can't use macro because
	// the field needs to be decomposed)
	if (resp->has_messages) {
		getlog_data->kgl_msgs    = (char *) resp->messages.data;
		getlog_data->kgl_msgslen = resp->messages.len;
	}

	// then other fields
	if (resp->capacity) {
		extract_primitive_optional(
			getlog_data->kgl_cap.kc_total,
			resp->capacity,
			nominalcapacityinbytes
		);

		extract_primitive_optional(
			getlog_data->kgl_cap.kc_used,
			resp->capacity, portionfull
		);
	}

	extract_result = extract_configuration(
		getlog_data,
		resp->configuration
	);
	if (extract_result < 0) { goto extract_glex; }

	// no allocations, so this "can't fail"
	extract_limits(getlog_data, resp->limits);

	if (resp->device && resp->device->has_name) {
		getlog_data->kgl_log.kdl_name = (char *)resp->device->name.data;
		getlog_data->kgl_log.kdl_len  = resp->device->name.len;
	}

	return krc;

 extract_glex:
	debug_printf("extract_getlog: error exit\n");
	destroy_getlog_ctx(ctx_pair);

	// if getlog data was saved; roll it back
	// NOTE: the allocations are tracked in ki ctx, so no need to free any
	//       pointers here, even on success
	if (needs_rollback) {
		memcpy(getlog_data, &rollback_data, sizeof(kgetlog_t));
	}

	// Just make sure we don't return an ok message
	if (krc == K_OK) { krc = K_EINTERNAL; }

	return krc;
}
