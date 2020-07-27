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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "kinetic.h"
#include "kinetic_internal.h"
#include "protocol_types.h"
#include "message.h"
#include "kio.h"

/**
 * This file holds the highest-level of the implementation of an interface to the protocol. We want
 * this to be as general as possible so that any backend can use the protocol, but we also want to
 * provide a reasonable default. The surface of the protocol interface is as follows:
 *
 * Message building for requests:
 *	- Each message must be built specially, but we hope to wrap them in a "narrow waist" of
 *	  constructor operators
 *	- Each message may have fields that are only for the request, and not for the response, and
 *	  vice versa. We want to highlight what the request-only fields are, to make clear what
 *	  lifetime these values must have.
 *
 * Message decoding for responses:
 *
 *
 * Extra, overall notes:
 *	- The unsolicited message at the beginning of a connection will return the drive status.
 *	- Proprietary names should be prefaced by the vendor name (e.g. "com.WD" for Western Digital
 *	  devices).
 *	- The limit of each log is 1m byte.
 */


// ------------------------------
// Message building for requests

// enum kproto_result_code kmessage_initialize(K

/*
 * Requests need only specify the `types` field, except for a "Device" message. If set, the
 * `device` field specifies that only "GetLog" info for that device should be returned. If the name
 * is not found, then the response has status, `NOT_FOUND`.
 */

typedef Com__Seagate__Kinetic__Proto__Message__HMACauth kauth_hmac;
typedef Com__Seagate__Kinetic__Proto__Message__PINauth	kauth_pin;


/* ------------------------------
 * Auth Functions
 */

int compute_hmac(kproto_msg_t *msg_data, char *key, uint32_t key_len) {
	int result_status;

	HMAC_CTX *hmac_context = HMAC_CTX_new();

	// to self-document that we are using the default ENGINE (or whatever this param is used for)
	ENGINE *hash_impl = NULL;

	result_status = HMAC_Init_ex(hmac_context, key, key_len, EVP_sha1(), hash_impl);
	if (!result_status) { return -1; }

	// TODO: what if the message has no command bytes?
	if (msg_data->has_commandbytes) {
		uint32_t msg_len_bigendian = htonl(msg_data->commandbytes.len);

		result_status = HMAC_Update(
			hmac_context,
			(unsigned char *) &msg_len_bigendian,
			sizeof(uint32_t)
		);
		if (!result_status) { return -1; }

		result_status = HMAC_Update(
			hmac_context,
			(unsigned char *) msg_data->commandbytes.data,
			msg_data->commandbytes.len
		);
		if (!result_status) { return -1; }
	}

	// finalize the digest into a string (allocated)
	void *hmac_digest = malloc(sizeof(char) * SHA_DIGEST_LENGTH);
	result_status = HMAC_Final(
		hmac_context,
		(unsigned char *) hmac_digest,
		(unsigned int *) &msg_data->hmacauth->hmac.len
	);
	if (!result_status) { return -1; }

	// free the HMAC context (leaves the result intact)
	HMAC_CTX_free(hmac_context);

	// set the hmac auth to the result
	msg_data->hmacauth->has_hmac = 1;
	msg_data->hmacauth->hmac     = (ProtobufCBinaryData) {
		.len  =             SHA_DIGEST_LENGTH,
		.data = (uint8_t *) hmac_digest,
	};

	return result_status;
}

// Data type helpers
char *helper_bytes_to_str(ProtobufCBinaryData proto_bytes) {
	size_t proto_len = proto_bytes.len;

	// allocate and copy byte data
	char *str_buffer = (char *) malloc(sizeof(char) * (proto_len + 1));
	memcpy(str_buffer, proto_bytes.data, proto_len);

	// null terminate the string
	str_buffer[proto_len] = '\0';

	return str_buffer;
}


// Protobuf convenience functions
struct kresult_message unpack_response(struct kbuffer response_buffer) {
	kproto_cmd_t *response_command = com__seagate__kinetic__proto__command__unpack(
		NULL,
		response_buffer.len,
		(uint8_t *) response_buffer.base
	);

	int unpacked_result_is_invalid = (
		   response_command				  == NULL
		|| response_command->body		  == NULL
		|| response_command->body->getlog == NULL
	);

	if (unpacked_result_is_invalid) {
		return (struct kresult_message) {
			.result_code	= FAILURE,
			.result_message = NULL
		};
	}

	return (struct kresult_message) {
		.result_code	= SUCCESS,
		.result_message = (void *) response_command
	};
}

ProtobufCBinaryData pack_kinetic_command(kproto_cmd_t *cmd_data) {
	// Get size for command and allocate buffer
	size_t	 command_size	= com__seagate__kinetic__proto__command__get_packed_size(cmd_data);
	uint8_t *command_buffer = (uint8_t *) malloc(sizeof(uint8_t) * command_size);

	if (command_buffer == NULL) { goto pack_failure; }

	size_t packed_bytes = com__seagate__kinetic__proto__command__pack(cmd_data, command_buffer);

	if (packed_bytes != command_size) {
		fprintf(
			stderr,
			"Unexpected amount of bytes packed. %ld bytes packed, expected %ld\n",
			packed_bytes,
			command_size
		);

		goto pack_failure;
	}

	return (ProtobufCBinaryData) { .len = packed_bytes, .data = command_buffer };

pack_failure:
	return (ProtobufCBinaryData) { .len = 0, .data = NULL };
}

ProtobufCBinaryData create_command_bytes(kproto_cmdhdr_t *cmd_hdr, void *proto_cmd,
		                                 kmtype_t msg_type) {
	// Structs to use
	kproto_cmd_t command_msg;
	kproto_body_t  command_body;

	// initialize the structs
	com__seagate__kinetic__proto__command__init(&command_msg);
	com__seagate__kinetic__proto__command__body__init(&command_body);

	// update the header for the Put Message Body
	cmd_hdr->messagetype = msg_type;

	// stitch the Command together
	switch(msg_type) {
		case KMT_GET:
		case KMT_GETVERS:
		case KMT_GETNEXT:
		case KMT_GETPREV:
		case KMT_PUT:
		case KMT_DEL:
			command_body.keyvalue = (kproto_keyval_t *) proto_cmd;
			break;

		case KMT_GETLOG:
			command_body.getlog   = (kproto_getlog_t *) proto_cmd;
			break;
	}

	command_msg.header	= cmd_hdr;
	command_msg.body	= &command_body;

	return pack_kinetic_command(&command_msg);
}

kproto_cmd_t *unpack_kinetic_command(ProtobufCBinaryData commandbytes) {
	// a NULL allocator defaults to system allocator (malloc)
	ProtobufCAllocator *mem_allocator = NULL;

	kproto_cmd_t *unpacked_cmd = com__seagate__kinetic__proto__command__unpack(
		mem_allocator, commandbytes.len, (uint8_t *) commandbytes.data
	);

	return unpacked_cmd;
}

enum kresult_code pack_kinetic_message(kproto_msg_t *msg_data, void **result_buffer, size_t *result_size) {
	// Get size for command and allocate buffer
	size_t msg_size     = com__seagate__kinetic__proto__message__get_packed_size(msg_data);
	uint8_t *msg_buffer = (uint8_t *) malloc(sizeof(uint8_t) * msg_size);

	if (msg_buffer == NULL) { return FAILURE; }

	size_t num_bytes_packed = com__seagate__kinetic__proto__message__pack(msg_data, msg_buffer);

	if (num_bytes_packed != msg_size) {
		fprintf(
			stderr,
			"Unexpected amount of bytes packed. %ld bytes packed, expected %ld\n",
			num_bytes_packed,
			msg_size
		);

		free(msg_buffer);
		return FAILURE;
	}

	*result_buffer = msg_buffer;
	*result_size   = msg_size;

	return SUCCESS;
}

struct kresult_message unpack_kinetic_message(void *response_buffer, size_t response_size) {
	// a NULL allocator defaults to system allocator (malloc)
	ProtobufCAllocator *mem_allocator = NULL;

	kproto_msg_t *unpacked_msg = com__seagate__kinetic__proto__message__unpack(
		mem_allocator, response_size, (uint8_t *) response_buffer
	);

	return (struct kresult_message) {
		.result_code    = unpacked_msg == NULL ? FAILURE : SUCCESS,
		.result_message = (void *) unpacked_msg
	};
}

struct kresult_message create_message(kmsghdr_t *msg_hdr, ProtobufCBinaryData cmd_bytes) {
	kproto_msg_t *kinetic_msg = (kproto_msg_t *) malloc(sizeof(kproto_msg_t));
	if (kinetic_msg == NULL) {
		goto create_failure;
	}

	com__seagate__kinetic__proto__message__init(kinetic_msg);

	if (cmd_bytes.len && cmd_bytes.data != NULL) {
		kinetic_msg->has_commandbytes = 1;
		kinetic_msg->commandbytes	  = cmd_bytes;
	}

	switch(msg_hdr->kmh_atype) {
		case KA_HMAC:
			kinetic_msg->has_authtype = 1;
			kinetic_msg->authtype	  = KA_HMAC;

			kauth_hmac *msg_auth_hmac = (kauth_hmac *) malloc(sizeof(kauth_hmac));
			com__seagate__kinetic__proto__message__hmacauth__init(msg_auth_hmac);

			kinetic_msg->hmacauth = msg_auth_hmac;

			msg_auth_hmac->has_identity = 1;
			msg_auth_hmac->identity		= msg_hdr->kmh_id;

			kinetic_msg->hmacauth->has_hmac = 1;
			kinetic_msg->hmacauth->hmac.data = msg_hdr->kmh_hmac;
			kinetic_msg->hmacauth->hmac.len  = strlen(msg_hdr->kmh_hmac);
#if 0
			int hmac_result = compute_hmac(kinetic_msg, (char *) msg_hdr->kmh_hmac, msg_hdr->kmh_hmaclen);
#endif
			break;

		case KA_PIN:
			kinetic_msg->has_authtype = 1;
			kinetic_msg->authtype	  = KA_PIN;

			kauth_pin *msg_auth_pin = (kauth_pin *) malloc(sizeof(kauth_pin));
			com__seagate__kinetic__proto__message__pinauth__init(msg_auth_pin);

			msg_auth_pin->has_pin = 1;
			msg_auth_pin->pin	  = (ProtobufCBinaryData) {
				.len  =             msg_hdr->kmh_pinlen,
				.data = (uint8_t *) msg_hdr->kmh_pin
			};

			kinetic_msg->pinauth = msg_auth_pin;
			break;

		case KA_INVALID:
		default:
			goto create_failure;
	};

	return (struct kresult_message) {
		.result_code	= SUCCESS,
		.result_message = kinetic_msg,
	};

create_failure:
	return (struct kresult_message) {
		.result_code	= FAILURE,
		.result_message = NULL,
	};
}

void extract_to_command_header(kproto_cmdhdr_t *proto_cmdhdr, kcmdhdr_t *cmdhdr_data) {

	com__seagate__kinetic__proto__command__header__init(proto_cmdhdr);

	proto_cmdhdr->has_clusterversion = 1;
	proto_cmdhdr->clusterversion     = cmdhdr_data->kch_clustvers;

	proto_cmdhdr->connectionid	 = cmdhdr_data->kch_connid;
	proto_cmdhdr->has_connectionid   = 1;

	proto_cmdhdr->messagetype	 = cmdhdr_data->kch_type;
	proto_cmdhdr->has_messagetype    = 1;

	proto_cmdhdr->sequence   	 = cmdhdr_data->kch_seq;
	proto_cmdhdr->has_sequence       = 1;

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

kstatus_t extract_cmdhdr(struct kresult_message *response_result, kcmdhdr_t *cmdhdr_data) {
	kproto_msg_t *response_msg = (kproto_msg_t *) response_result->result_message;

	// commandbytes should exist, but we should probably be thorough
	if (!response_msg->has_commandbytes) {
		return (kstatus_t) {
			.ks_code    = K_INVALID_SC,
			.ks_message = "Response message has no command bytes",
			.ks_detail  = NULL
		};
	}

	// unpack the command bytes into a command structure
	kproto_cmd_t *response_cmd = unpack_kinetic_command(response_msg->commandbytes);
	kproto_cmdhdr_t *response_cmd_hdr = response_cmd->header;

	assign_if_set(cmdhdr_data->kch_clustvers, response_cmd_hdr, clusterversion);
	assign_if_set(cmdhdr_data->kch_connid   , response_cmd_hdr, connectionid);
	assign_if_set(cmdhdr_data->kch_timeout  , response_cmd_hdr, timeout);
	assign_if_set(cmdhdr_data->kch_pri      , response_cmd_hdr, priority);
	assign_if_set(cmdhdr_data->kch_quanta   , response_cmd_hdr, timequanta);
	assign_if_set(cmdhdr_data->kch_batid    , response_cmd_hdr, batchid);

	// ------------------------------
	// propagate the response status to the caller
	kproto_status_t *response_status = response_cmd->status;

	// copy the status message so that destroying the unpacked command doesn't get weird
	size_t statusmsg_len     = strlen(response_status->statusmessage);
	char *response_statusmsg = (char *) malloc(sizeof(char) * statusmsg_len);
	strcpy(response_statusmsg, response_status->statusmessage);

	char *response_detailmsg = NULL;
	if (response_status->has_detailedmessage) {
		response_detailmsg = (char *) malloc(sizeof(char) * response_status->detailedmessage.len);
		memcpy(
			response_detailmsg,
			response_status->detailedmessage.data,
			response_status->detailedmessage.len
		);
	}

	// cleanup before we return the status data
	destroy_command(response_cmd);

	return (kstatus_t) {
		.ks_code    = response_status->has_code ? response_status->code : K_INVALID_SC,
		.ks_message = response_statusmsg,
		.ks_detail  = response_detailmsg,
	};
}

int keyname_to_proto(kproto_kv_t *proto_keyval, kv_t *cmd_data) {
	// return error if params don't meet assumptions
	if (proto_keyval == NULL || cmd_data == NULL) { return -1; }

	size_t *cumulative_offsets = (size_t *) malloc(sizeof(size_t) * cmd_data->kv_keycnt);
	if (cumulative_offsets == NULL) { return -1; }

	size_t total_keylen = 0;
	for (size_t key_ndx = 0; key_ndx < cmd_data->kv_keycnt; key_ndx++) {
		cumulative_offsets[key_ndx] = total_keylen;
		total_keylen += cmd_data->kv_key[key_ndx].kiov_len;
	}

	// create a buffer containing the key name
	char *key_buffer = (char *) malloc(sizeof(char) * total_keylen);

	// cleanup on a malloc failure
	if (key_buffer == NULL) {
		free(cumulative_offsets);
		return -1;
	}

	// gather key name fragments into key buffer
	for (size_t key_ndx = 0; key_ndx < cmd_data->kv_keycnt; key_ndx++) {
		memcpy(
			key_buffer + cumulative_offsets[key_ndx],
			cmd_data->kv_key[key_ndx].kiov_base,
			cmd_data->kv_key[key_ndx].kiov_len
		);
	}

	// this array was only needed for the gather
	free(cumulative_offsets);

	// key_buffer eventually needs to be `free`d
	set_bytes_optional(proto_keyval, key, key_buffer, total_keylen);

	return 0;
}


/* ------------------------------
 * Helper functions for ktli
 */

// TODO: remove unnecessary allocations later
uint64_t ki_getaseq(struct kiovec *msg, int msgcnt) {
	uint64_t ack_seq = -1;

	//ERROR: not enough messages
	if (KIOV_MSG >= msgcnt) { return 0; }

	// walk the message first
	struct kresult_message unpack_result = unpack_kinetic_message(
		msg[KIOV_MSG].kiov_base, msg[KIOV_MSG].kiov_len
	);

	if (unpack_result.result_code == FAILURE) {
		//TODO: we won't allocate in the future, but we should figure out what to do for errors
		return 0;
	}

	kproto_msg_t *tmp_msg = (kproto_msg_t *) unpack_result.result_message;


	// then walk the command
	kproto_cmd_t *tmp_cmd = unpack_kinetic_command(tmp_msg->commandbytes);

	// extract the ack sequence field
	if (tmp_cmd->header->has_acksequence) {
		ack_seq = tmp_cmd->header->acksequence;
	}

	// TODO: since we allocate currently, we need to clean up
	destroy_command(tmp_cmd);
	destroy_message(tmp_msg);

	return ack_seq;
}

// TODO: remove unnecessary allocations later
void ki_setseq(struct kiovec *msg, int msgcnt, uint64_t seq) {

	kpdu_t pdu;

	// ERROR: not enough messages
	if (KIOV_MSG >= msgcnt) { return; }

	// walk the message first
	struct kresult_message unpack_result = unpack_kinetic_message(
		msg[KIOV_MSG].kiov_base, msg[KIOV_MSG].kiov_len
	);

	if (unpack_result.result_code == FAILURE) {
		//TODO: we won't allocate in the future, but we should figure out what to do for errors
		//return -1;
		;
	}

	kproto_msg_t *tmp_msg = (kproto_msg_t *) unpack_result.result_message;

	// then walk the command
	kproto_cmd_t *tmp_cmd = unpack_kinetic_command(tmp_msg->commandbytes);

	// extract the ack sequence field
	tmp_cmd->header->has_sequence = 1;
	tmp_cmd->header->sequence     = seq;

	// pack field (TODO: currently this packs the whole thing, but eventually we would like to pack
	// just the new field)
	tmp_msg->commandbytes = pack_kinetic_command(tmp_cmd);

	int hmac_result = compute_hmac(tmp_msg,
				       (char *) tmp_msg->hmacauth->hmac.data,
				       tmp_msg->hmacauth->hmac.len);

	enum kresult_code repack_result = pack_kinetic_message(
		tmp_msg,
		&(msg[KIOV_MSG].kiov_base),
		&(msg[KIOV_MSG].kiov_len)
	);

	/*
	 * Adding the final seq and adding the real HMAC changes the 
	 * message length, Unpack the pdu, update it and repack
	*/
	UNPACK_PDU(&pdu, (uint8_t *)msg[KIOV_PDU].kiov_base);
	pdu.kp_msglen = msg[KIOV_MSG].kiov_len;
	PACK_PDU(&pdu, (uint8_t *)msg[KIOV_PDU].kiov_base);

	// TODO: since we allocate currently, we need to clean up
	destroy_command(tmp_cmd);
	destroy_message(tmp_msg);
}

/* ------------------------------
 * Resource management functions
 */

void destroy_message(void *unpacked_msg) {
	// At some point, it would be best to make sure the allocator used in `unpack` is used here
	ProtobufCAllocator *mem_allocator = NULL;

	com__seagate__kinetic__proto__message__free_unpacked(
		(kproto_msg_t *) unpacked_msg,
		mem_allocator
	);
}

void destroy_command(void *unpacked_cmd) {
	// At some point, it would be best to make sure the allocator used in `unpack` is used here
	ProtobufCAllocator *mem_allocator = NULL;

	com__seagate__kinetic__proto__command__free_unpacked(
		(kproto_cmd_t *) unpacked_cmd,
		mem_allocator
	);
}
