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

#include "kinetic.h"
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

			msg_auth_hmac->has_identity = 1;
			msg_auth_hmac->identity		= msg_hdr->kmh_id;

			msg_auth_hmac->has_hmac = 1;
			msg_auth_hmac->hmac		= (ProtobufCBinaryData) {
				.len  =             msg_hdr->kmh_hmaclen,
				.data = (uint8_t *) msg_hdr->kmh_hmac
			};

			kinetic_msg->hmacauth = msg_auth_hmac;

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

// TODO: this is a helper fn, picked from protobuf-c source, to assist in walking protobuf code
/* NOTE: not yet used, so just commenting out
static size_t
parse_tag_and_wiretype(size_t len, const uint8_t *data, uint32_t *tag_out, ProtobufCWireType *wiretype_out) {
	/* Bit fields for reference
	 *     0xf8: 1111 1000    0x80: 1000 0000
	 *     0x07: 0000 0111    0x7f: 0111 1111
	 *
	unsigned max_rv          = len > 5 ? 5 : len;
	uint32_t tag             = (data[0] & 0x7f) >> 3;
	unsigned half_byte_width = 4;
	unsigned rv;

	// 0 is not a valid tag value
	if ((data[0] & 0xf8) == 0) { return 0; }

	// grab the field's wiretype (type used for serializing to the wire)
	*wiretype_out = data[0] & 7;

	// significant bit tells us if the next byte is part of the tag
	if ((data[0] & 0x80) == 0) {
		*tag_out = tag;
		return 1;
	}

	// for each byte, up to max_rv (max of 5 and len)
	for (rv = 1; rv < max_rv; rv++) {

		// if the significant bit is set, accumulate into the tag
		if (data[rv] & 0x80) {
			tag |= (data[rv] & 0x7f) << half_byte_width;
			half_byte_width += 7;
		}

		// otherwise, accumulate this one and return how many bytes were accumulated
		else {
			tag |= data[rv] << half_byte_width;
			*tag_out = tag;
			return rv + 1;
		}
	}

	// error: bad header
	return 0;
}
*/

// TODO: this is going to be used for results of walking the protobuf data
struct protobuf_loc {
	size_t byte_offset;
	size_t wiretype_len;
	uint8_t (*unpack_field)(uint8_t *data_buffer, size_t byte_offset, size_t len);
	uint8_t (*pack_field)(uint8_t *data_buffer, size_t byte_offset, size_t len);
};


// TODO: it's really painful, but this is implemented for functionality first. will remove
// unnecessary allocations later
uint64_t ki_getaseq(struct kiovec *msg, int msgcnt) {
	int64_t ack_seq = -1;

	//ERROR: not enough messages
	if (KIOV_MSG >= msgcnt) { return 0; }

	// walk the message first
	struct kresult_message unpack_result = unpack_kinetic_message(
		msg[KIOV_MSG].kiov_base, msg[KIOV_MSG].kiov_len
	);

	if (unpack_result.result_code == FAILURE) {
		//TODO: we won't allocate in the future, but we should figure out what to do for errors
		return -1;
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

// TODO: it's really painful, but this is implemented for functionality first. will remove
// unnecessary allocations later
void ki_setseq(struct kiovec *msg, int msgcnt, uint64_t seq) {

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

	enum kresult_code repack_result = pack_kinetic_message(
		tmp_msg,
		&(msg->kiov_base),
		&(msg->kiov_len)
	);

	// TODO: since we allocate currently, we need to clean up
	destroy_command(tmp_cmd);
	destroy_message(tmp_msg);
}
