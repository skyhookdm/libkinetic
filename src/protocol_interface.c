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
	char *str_buffer = (char *) KI_MALLOC(sizeof(char) * (proto_len + 1));
	if (!str_buffer) { return NULL; }

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
		debug_fprintf(
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
	uint8_t *msg_buffer = (uint8_t *) KI_MALLOC(sizeof(uint8_t) * msg_size);

	if (msg_buffer == NULL) { return FAILURE; }

	size_t num_bytes_packed = com__seagate__kinetic__proto__message__pack(msg_data, msg_buffer);

	if (num_bytes_packed != msg_size) {
		debug_fprintf(
			stderr,
			"Unexpected amount of bytes packed. %ld bytes packed, expected %ld\n",
			num_bytes_packed,
			msg_size
		);

		KI_FREE(msg_buffer);
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

/*
 * Handles boilerplate code for creating and stitching together a kinetic `Command` and returns the
 * packed result (serialized to wire format). The message type
 */
// ProtobufCBinaryData create_command_bytes(kproto_cmdhdr_t *cmd_hdr, void *proto_cmd_data) {
ProtobufCBinaryData create_command_bytes(kcmdhdr_t *cmd_hdr, void *proto_cmd_data) {
	// Structs to use
	kproto_cmdhdr_t proto_cmd_hdr;
	kproto_cmd_t    proto_cmd;
	kproto_body_t   proto_cmdbdy;

	// initialize the structs
	com__seagate__kinetic__proto__command__init(&proto_cmd);
	com__seagate__kinetic__proto__command__body__init(&proto_cmdbdy);

	// populate protobuf command header struct
	extract_to_command_header(&proto_cmd_hdr, cmd_hdr);

	// stitch the Command together
	switch(proto_cmd_hdr.messagetype) {
		case KMT_GET:
		case KMT_GETVERS:
		case KMT_GETNEXT:
		case KMT_GETPREV:
		case KMT_PUT:
		case KMT_DEL:
			proto_cmdbdy.keyvalue = (kproto_kv_t *) proto_cmd_data;
			break;

		case KMT_GETLOG:
			proto_cmdbdy.getlog   = (kproto_getlog_t *) proto_cmd_data;
			break;

		case KMT_GETRANGE:
			proto_cmdbdy.range    = (kproto_keyrange_t *) proto_cmd_data;
			break;

		case KMT_STARTBAT:
		case KMT_ENDBAT:
			proto_cmdbdy.batch    = (kproto_batch_t *) proto_cmd_data;
			break;
	}

	proto_cmd.header = &proto_cmd_hdr;
	proto_cmd.body	 = &proto_cmdbdy;

	return pack_kinetic_command(&proto_cmd);
}

struct kresult_message create_message(kmsghdr_t *msg_hdr, ProtobufCBinaryData cmd_bytes) {
	kproto_msg_t *kinetic_msg = (kproto_msg_t *) KI_MALLOC(sizeof(kproto_msg_t));
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

			kauth_hmac *msg_auth_hmac = (kauth_hmac *) KI_MALLOC(sizeof(kauth_hmac));
			if (!msg_auth_hmac) { goto create_failure; }

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

			kauth_pin *msg_auth_pin = (kauth_pin *) KI_MALLOC(sizeof(kauth_pin));
			if (!msg_auth_pin) { goto create_failure; }

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

	if (cmdhdr_data->kch_bid) {
		proto_cmdhdr->batchid	  = cmdhdr_data->kch_bid;
		proto_cmdhdr->has_batchid = 1;
	}
}

kstatus_t extract_cmdstatus(kproto_cmd_t *protobuf_command) {
	if (!protobuf_command || !protobuf_command->status || !protobuf_command->status->has_code) {
		return kstatus_err(K_OK, KI_ERR_NOMSG, "");
	}

	kproto_status_t *response_status = protobuf_command->status;

	// copy protobuf string
	size_t statusmsg_len     = strlen(response_status->statusmessage);
	char *response_statusmsg = (char *) KI_MALLOC(sizeof(char) * statusmsg_len);
	if (!response_statusmsg) { return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, ""); }

	strcpy(response_statusmsg, response_status->statusmessage);

	// copy protobuf bytes field to null-terminated string
	char *response_detailmsg = NULL;
	if (response_status->has_detailedmessage) {
		response_detailmsg = helper_bytes_to_str(response_status->detailedmessage);

		// if we fail to malloc in helper_bytes_to_str()
		if (!response_detailmsg) {
			KI_FREE(response_statusmsg);
			return kstatus_err(K_EINTERNAL, KI_ERR_MALLOC, "");
		}
	}

	return (kstatus_t) {
		.ks_code    = response_status->code,
		.ks_message = response_statusmsg,
		.ks_detail  = response_detailmsg,
	};
}

kstatus_t extract_cmdhdr(struct kresult_message *response_result, kcmdhdr_t *cmdhdr_data) {
	kstatus_t extracted_status = kstatus_err(K_INVALID_SC, KI_ERR_NOMSG, "");
	kproto_msg_t *response_msg = (kproto_msg_t *) response_result->result_message;

	// commandbytes should exist, but we should probably be thorough
	if (!response_msg || !response_msg->has_commandbytes) {
		return kstatus_err(K_INVALID_SC, KI_ERR_NOCMD, "extract command header");
	}

	// unpack the command bytes into a command structure
	kproto_cmd_t *response_cmd = unpack_kinetic_command(response_msg->commandbytes);
	if (!response_cmd) {
		return kstatus_err(K_EINTERNAL, KI_ERR_CMDUNPACK, "extract command header");
	}

	kproto_cmdhdr_t *response_cmd_hdr = response_cmd->header;

	extract_primitive_optional(cmdhdr_data->kch_clustvers, response_cmd_hdr, clusterversion);
	extract_primitive_optional(cmdhdr_data->kch_connid   , response_cmd_hdr, connectionid);
	extract_primitive_optional(cmdhdr_data->kch_timeout  , response_cmd_hdr, timeout);
	extract_primitive_optional(cmdhdr_data->kch_pri      , response_cmd_hdr, priority);
	extract_primitive_optional(cmdhdr_data->kch_quanta   , response_cmd_hdr, timequanta);
	extract_primitive_optional(cmdhdr_data->kch_bid      , response_cmd_hdr, batchid);

	// extract the kinetic response status; copy the messages for independence from the body data
	extracted_status = extract_cmdstatus(response_cmd);
	/*
	 * TODO: check that this is still covered
	 *
	 * else {
	 * 	// status in the message but everything went OK
	 * 	extracted_status = (kstatus_t) {
	 * 		.ks_code    = K_OK,
	 * 		.ks_message = "",
	 * 		.ks_detail  = "",
	 * 	};
	 * }
	 */

	// cleanup before we return the status data
	destroy_command(response_cmd);

	return extracted_status;
}

size_t calc_total_len(struct kiovec *byte_fragments, size_t fragment_count) {
	size_t total_len = 0;

	for (size_t fragment_ndx = 0; fragment_ndx < fragment_count; fragment_ndx++) {
		total_len += byte_fragments[fragment_ndx].kiov_len;
	}

	return total_len;
}

int keyname_to_proto(ProtobufCBinaryData *proto_keyname, struct kiovec *keynames, size_t keycnt) {
	// return error if params don't meet assumptions
	if (keynames == NULL) { return 0; }

	size_t *cumulative_offsets = (size_t *) malloc(sizeof(size_t) * keycnt);
	if (cumulative_offsets == NULL) { return 0; }

	size_t total_keylen = 0;
	for (size_t key_ndx = 0; key_ndx < keycnt; key_ndx++) {
		cumulative_offsets[key_ndx] = total_keylen;
		total_keylen += keynames[key_ndx].kiov_len;
	}

	// create a buffer containing the key name
	char *key_buffer = (char *) malloc(sizeof(char) * total_keylen);

	// cleanup on a malloc failure
	if (key_buffer == NULL) {
		free(cumulative_offsets);
		return 0;
	}

	// gather key name fragments into key buffer
	for (size_t key_ndx = 0; key_ndx < keycnt; key_ndx++) {
		memcpy(
			key_buffer + cumulative_offsets[key_ndx],
			keynames[key_ndx].kiov_base,
			keynames[key_ndx].kiov_len
		);
	}

	// this array was only needed for the gather
	free(cumulative_offsets);

	// key_buffer eventually needs to be `free`d
	*proto_keyname = (ProtobufCBinaryData) {
		.data = (uint8_t *) key_buffer,
		.len  =             total_keylen,
	};

	return 1;
}


/* ------------------------------
 * Helper functions for ktli
 */

// TODO: remove unnecessary allocations later
uint64_t ki_getaseq(struct kiovec *msg, int msgcnt) {
	uint64_t ack_seq = -1;
	kpdu_t pdu;

	//ERROR: not enough messages
	if (KIOV_MSG >= msgcnt) { return 0; }

	/* 
	 * Now unpack the message remember KIOV_MSG 
	 * may contain both msg and value
	 */
	UNPACK_PDU(&pdu, (uint8_t *)msg[KIOV_PDU].kiov_base);
	// walk the message first
	struct kresult_message unpack_result = unpack_kinetic_message(
		msg[KIOV_MSG].kiov_base, pdu.kp_msglen 
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
