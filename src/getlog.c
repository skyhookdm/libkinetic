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
#include "kinetic_internal.h"

// #include "message.h"
#include "protocol_interface.h"

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
	int rc;
	kstatus_t krc;
	struct kio *kio;
	struct kiovec *kiov;
	struct ktli_config *cf;
	uint8_t ppdu[KP_PLENGTH];
	kpdu_t pdu;
	kpdu_t *rpdu;
	kmsghdr_t msg_hdr;
	kcmdhdr_t cmd_hdr;
	kgetlog_t glog2;
	ksession_t *ses;
	struct kresult_message kmreq, kmresp;

	/* Get KTLI config */ 
	rc = ktli_config(ktd, &cf);
	if (rc < 0) { return kstatus_err(K_EREJECTED, KI_ERR_BADSESS, ""); }

	ses = (ksession_t *)cf->kcfg_pconf;

	/* Validate the passed in glog; this function sets errno */
	rc = ki_validate_glog(glog);
	if (rc < 0) { return kstatus_err(errno, KI_ERR_INVARGS, "getlog: validation"); }

	/* create the kio structure; first malloc, so we return on failure */
	kio = (struct kio *) KI_MALLOC(sizeof(struct kio));
	if (!kio) { return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "getlog: kio"); }

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
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hkey;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type      = KMT_GETLOG;

	kmreq = create_getlog_message(&msg_hdr, &cmd_hdr, glog);
	if (kmreq.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_CREATEREQ, "getlog: request");
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
	kio->kio_sendmsg.km_cnt = 2;
	kio->kio_sendmsg.km_msg = (struct kiovec *) KI_MALLOC(
		sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt
	);

	if (!kio->kio_sendmsg.km_msg) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "getlog: kio_sendmsg");
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
		errno = K_EINTERNAL;
		krc   = kstatus_err(errno, KI_ERR_MSGPACK, "getlog: pack msg");
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
	debug_printf ("Sent Kio: %p\n", kio);

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
				krc = kstatus_err(K_EINTERNAL, KI_ERR_RECVMSG, "getlog: recvmsg");
				goto glex_sendmsg;
			}
		}

		/* Got our response */
		else { break; }
	} while (1);

	kiov   = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);
	if (kmresp.result_code == FAILURE) {
		krc = kstatus_err(K_EINTERNAL, KI_ERR_MSGUNPACK, "getlog: unpack response");
		goto glex_recvmsg;
	}

	/* NOTES:
	 *	- we use glog2 here in case we want to validate it separately 
	 *	  from glog
	 *	- on success, we memcpy glog2. So _don't_ destroy it, 
	 *	  otherwise glog will have free'd  pointers.
	 *	- on failure, extract_getlog will have already destroyed 
	 *	  glog2 as much as possible
	 */
	krc = extract_getlog(&kmresp, &glog2);

	// if successful, memcpy into glog (no separate validation at the moment)
	if (krc.ks_code == K_OK) {
		memcpy(glog, &glog2, sizeof(kgetlog_t));
	}
	else {
		glog2.destroy_protobuf(&glog2);
	}

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
		getlog_if_data[if_ndx]->ki_name = if_data[if_ndx]->name;

		if (if_data[if_ndx]->has_mac) {
			getlog_if_data[if_ndx]->ki_mac = helper_bytes_to_str(if_data[if_ndx]->mac);
		}
		if (if_data[if_ndx]->has_ipv4address) {
			getlog_if_data[if_ndx]->ki_ipv4 = helper_bytes_to_str(if_data[if_ndx]->ipv4address);
		}
		if (if_data[if_ndx]->has_ipv6address) {
			getlog_if_data[if_ndx]->ki_ipv6 = helper_bytes_to_str(if_data[if_ndx]->ipv6address);
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
	// NOTE: serial and wwn are `free`d in `destroy_protobuf_getlog`
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

// This may get a partially defined structure if we hit an error during the construction.
// NOTE: it's possible we will only want to set this function if getlog_data was successfully
//       created (and so on errors, everything gets free'd immediately)
void destroy_protobuf_getlog(kgetlog_t *getlog_data) {
	// Don't do anything if we didn't get a valid pointer
	if (!getlog_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_getlog_t *) getlog_data->kgl_protobuf);

	// then free arrays of pointers that point to the message data
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

kstatus_t extract_getlog(struct kresult_message *response_msg, kgetlog_t *getlog_data) {
	kstatus_t getlog_status           = kstatus_err(K_EINTERNAL, KI_ERR_NOMSG, "");
	kproto_msg_t *getlog_response_msg = (kproto_msg_t *) response_msg->result_message;

	// check that there is command data
	if (!getlog_response_msg->has_commandbytes) {
		return kstatus_err(K_EINTERNAL, KI_ERR_NOCMD, "getlog: extract");
	}

	kproto_cmd_t *response_cmd = unpack_kinetic_command(getlog_response_msg->commandbytes);
	if (!response_cmd) {
		return kstatus_err(K_EINTERNAL, KI_ERR_CMDUNPACK, "getlog: extract");
	}
	getlog_data->kgl_protobuf = response_cmd;

	// extract the kinetic response status; copy the data for data independence
	getlog_status = extract_cmdstatus(response_cmd);
	if (getlog_status.ks_code != K_OK) { goto extract_glex; }

	// check that there's a command body and getlog data
	if (!response_cmd->body || !response_cmd->body->getlog) { return getlog_status; }

	// begin extraction of command body into getlog structure
	kproto_getlog_t *response = response_cmd->body->getlog;

	// 0 is success, < 0 is failure. Use this for all the extract functions
	int extract_result;

	getlog_data->kgl_typecnt = response->n_types;
	getlog_data->kgl_type    = response->types;

	// repeated fields
	extract_result = extract_utilizations(
		getlog_data, response->n_utilizations, response->utilizations
	);
	if (extract_result < 0) { goto extract_glex; }

	extract_result = extract_temperatures(
		getlog_data, response->n_temperatures, response->temperatures
	);
	if (extract_result < 0) { goto extract_glex; }

	extract_result = extract_statistics(getlog_data, response->n_statistics, response->statistics);
	if (extract_result < 0) { goto extract_glex; }

	// then optional fields (can't use macro because the field needs to be decomposed)
	if (response->has_messages) {
		getlog_data->kgl_msgs    = (char *) response->messages.data;
		getlog_data->kgl_msgslen = response->messages.len;
	}

	// then other fields
	if (response->capacity) {
		extract_primitive_optional(
			getlog_data->kgl_cap.kc_total, response->capacity, nominalcapacityinbytes
		);

		extract_primitive_optional(
			getlog_data->kgl_cap.kc_used , response->capacity, portionfull
		);
	}

	extract_result = extract_configuration(getlog_data, response->configuration);
	if (extract_result < 0) { goto extract_glex; }

	// no allocations, so this "can't fail"
	extract_limits(getlog_data, response->limits);

	if (response->device && response->device->has_name) {
		getlog_data->kgl_log.kdl_name = response->device->name.data;
		getlog_data->kgl_log.kdl_len  = response->device->name.len;
	}

	// set destructor to be called later
	getlog_data->destroy_protobuf = destroy_protobuf_getlog;

	return getlog_status;

 extract_glex:
	// set this so `destroy_protobuf_getlog` can correctly destroy it
	destroy_protobuf_getlog(getlog_data);

	// Just make sure we don't return an ok message
	if (getlog_status.ks_code == K_OK) { getlog_status.ks_code = K_EINTERNAL; }

	return getlog_status;
}
