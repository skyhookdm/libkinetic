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
struct kresult_message create_put_message(kmsghdr_t *, kcmdhdr_t *, kv_t *, kcachepolicy_t, int);

/**
 * ki_put(int ktd, kv_t *kv)
 *
 *  kv		kv_key must contain a fully populated kiovec array
 *		kv_val must contain a zero-ed kiovec array of cnt 1
 * 		kv_vers and kv_verslen are optional
 * 		kv_tag and kv_taglen are optional. 
 *		kv_ditype is returned by the server, but it should 
 * 		have either a 0 or a valid ditype in it to start with
 *
 * Put the value specified by the given key, tag, and version. 
 *
 */
kstatus_t
ki_put(int ktd, kv_t *kv)
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

	/* Get KTLI config */
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
	rc = ki__validate_kv(kv, &ses->ks_l);
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
	kio->kio_cmd = KMT_PUT;
	kio->kio_sendmsg.km_msg[0].kiov_base = (void *) &pdu;
	kio->kio_sendmsg.km_msg[0].kiov_len = KP_LENGTH;

	/* Setup msg_hdr */
	memset((void *) &msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.kmh_atype = KA_HMAC;
	msg_hdr.kmh_id    = cf->kcfg_id;
	msg_hdr.kmh_hmac  = cf->kcfg_hmac;

	/* Setup cmd_hdr */
	memcpy((void *) &cmd_hdr, (void *) &ses->ks_ch, sizeof(cmd_hdr));
	cmd_hdr.kch_type      = KMT_PUT;

	// hardcoding reasonable defaults: writeback with no force
	kcachepolicy_t cache_opt = KC_WB;
	int            bool_shouldforce = 0;
	kmreq = create_put_message(&msg_hdr, &cmd_hdr, kv, cache_opt, bool_shouldforce);
	if (kmreq.result_code == FAILURE) {
		goto gex2;
	}

	/* pack the message and hang it on the kio */
	/* PAK: Error handling */
	/* success: rc = 0; failure: rc = 1 (see enum kresult_code) */
	rc = pack_kinetic_message(
		(kproto_msg_t *) &(kmreq.result_message),
		&(kio->kio_sendmsg.km_msg[1].kiov_base),
		&(kio->kio_sendmsg.km_msg[1].kiov_len)
	);

	/* Now that the message length is known, setup the PDU */
	pdu.kp_magic  = KP_MAGIC;
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
	kmresp = unpack_put_resp(kio->kio_recvmsg.km_msg[1].kiov_base,
				    kio->kio_recvmsg.km_msg[1].kiov_len);

	if (kmresp->result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto gex2;
	}

	kstatus = extract_put(kmresp, kv);
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
		.ks_message = "Error sending putlog request: glex2",
		.ks_detail  = "",
	};
}

/*
 * Helper functions
 */
struct kresult_message create_put_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
                                          kv_t *cmd_data, kcachepolicy_t cache_opt,
                                          int bool_shouldforce) {

	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_kv_t     proto_cmd_body;

	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// extract from cmd_data into proto_cmd_body
	int extract_result = keyname_to_proto(&proto_cmd_body, cmd_data);
	if (extract_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// if the keyval has a version set, then it is passed as newversion and we need to pass the old
	// version as dbversion
	if (cmd_data->kv_vers != NULL && cmd_data->kv_verslen > 0) {
		set_bytes_optional(&proto_cmd_body, newversion, cmd_data->kv_vers  , cmd_data->kv_verslen  );
		set_bytes_optional(&proto_cmd_body, dbversion , cmd_data->kv_dbvers, cmd_data->kv_dbverslen);
	}

	// we could potentially compute tag here (based on integrity algorithm) if desirable
	set_primitive_optional(&proto_cmd_body, algorithm, cmd_data->kv_ditype);
	set_bytes_optional(&proto_cmd_body, tag, cmd_data->kv_tag, cmd_data->kv_taglen);

	// if force is specified, then the dbversion is essentially ignored.
	set_primitive_optional(&proto_cmd_body, force          , bool_shouldforce ? 1 : 0);
	set_primitive_optional(&proto_cmd_body, synchronization, cache_opt               );

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, (void *) &proto_cmd_body, KMT_PUT
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	free(proto_cmd_body.key.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}
