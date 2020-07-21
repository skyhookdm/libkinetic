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

	// to self-document that we are using the default ENGINE (or whatever this param is used for)
	ENGINE *hash_impl = NULL;

	HMAC_CTX *hmac_context = HMAC_CTX_new();

	result_status = HMAC_Init_ex(hmac_context, key, key_len, EVP_sha1(), hash_impl);
	if (!result_status) { return -1; }

	if (msg_data->has_commandbytes) {
		uint32_t msg_len_bigendian = htonl(key_len);

		result_status = HMAC_Update(hmac_context, (char *) &msg_len_bigendian, sizeof(uint32_t));
		if (!result_status) { return -1; }

		result_status = HMAC_Update(hmac_context, key, key_len);
		if (!result_status) { return -1; }
	}

	void *hmac_digest = malloc(sizeof(char) * SHA_DIGEST_LENGTH);

	msg_data->hmacauth->has_hmac = 1;
	msg_data->hmacauth->hmac     = (ProtobufCBinaryData) {
		.len  =             SHA_DIGEST_LENGTH,
		.data = (uint8_t *) hmac_digest,
	};

	result_status = HMAC_Final(
		hmac_context,
		(unsigned char *) hmac_digest,
		(unsigned int *) &msg_data->hmacauth->hmac.len
	);
	if (!result_status) { return -1; }


	HMAC_CTX_free(hmac_context);

	return result_status;
}


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

			kinetic_msg->hmacauth = msg_auth_hmac;

			msg_auth_hmac->has_identity = 1;
			msg_auth_hmac->identity		= msg_hdr->kmh_id;

			int hmac_result = compute_hmac(kinetic_msg, (char *) msg_hdr->kmh_hmac, msg_hdr->kmh_hmaclen);

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

	char *status_detail_msg = response_status->has_detailedmessage ?
		  (char *) response_status->detailedmessage.data
		: NULL
	;

	return (kstatus_t) {
		.ks_code    = response_status->has_code ? response_status->code : K_INVALID_SC,
		.ks_message = response_status->statusmessage,
		.ks_detail  = status_detail_msg
	};
}

// TODO: this is a helper fn, picked from protobuf-c source, to assist in walking protobuf code
static size_t
parse_tag_and_wiretype(size_t len, const uint8_t *data, uint32_t *tag_out, ProtobufCWireType *wiretype_out) {
	/* Bit fields for reference
	 *     0xf8: 1111 1000    0x80: 1000 0000
	 *     0x07: 0000 0111    0x7f: 0111 1111
	 */
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

// TODO: this is going to be used for results of walking the protobuf data
struct protobuf_loc {
	size_t byte_offset;
	size_t wiretype_len;
	uint8_t (*unpack_field)(uint8_t *data_buffer, size_t byte_offset, size_t len);
	uint8_t (*pack_field)(uint8_t *data_buffer, size_t byte_offset, size_t len);
};


#define BOUND_SIZEOF_SCANNED_MEMBER_LOG2    5
#define FIRST_SCANNED_MEMBER_SLAB_SIZE_LOG2 4

#define MAX_SCANNED_MEMBER_SLAB (                           \
	  sizeof(unsigned int) * 8 - 1			                \
	- BOUND_SIZEOF_SCANNED_MEMBER_LOG2	                    \
	- FIRST_SCANNED_MEMBER_SLAB_SIZE_LOG2                   \
)

#define REQUIRED_FIELD_BITMAP_SET(index) (                  \
	required_fields_bitmap[(index)/8] |= (1UL<<((index)%8)) \
)

#define REQUIRED_FIELD_BITMAP_IS_SET(index) (               \
	  required_fields_bitmap[(index)/8]                     \
	& ( 1UL << ((index) % 8) )                              \
)


struct protobuf_loc *
protobuf_c_field_seek(const ProtobufCMessageDescriptor *desc, size_t len, const uint8_t *data)
{

	const ProtobufCFieldDescriptor *last_field = desc->fields + 0;

	ScannedMember first_member_slab[1UL << FIRST_SCANNED_MEMBER_SLAB_SIZE_LOG2];

	// scanned_member_slabs is an array of arrays
	ScannedMember *scanned_member_slabs[MAX_SCANNED_MEMBER_SLAB + 1];
	scanned_member_slabs[0] = first_member_slab;

	unsigned which_slab       = 0; // the slab we are currently populating
	unsigned in_slab_index    = 0; // number of members in the slab
	size_t   n_unknown        = 0;
	unsigned last_field_index = 0;
	unsigned f;
	unsigned j;
	unsigned i_slab;

	ProtobufCMessage *rv;
	rv = malloc(desc->sizeof_message);
	if (!rv) { return (NULL); }

	// Generated code always defines "message_init". However, we provide a
	// fallback for (1) users of old protobuf-c generated-code that do not
	// provide the function, and (2) descriptors constructed from some other
	// source (most likely, direct construction from the .proto file).
	if (desc->message_init != NULL) { protobuf_c_message_init(desc, rv); }
	else { message_init_generic(desc, rv); }

	// rem is the number of bytes remaining; at is the byte we're on
	size_t rem        = len;
	const uint8_t *at = data;

	while (rem > 0) {
		// parse the tag and type of the next field
		uint32_t          tag;
		ProtobufCWireType wire_type;
		size_t used = parse_tag_and_wiretype(rem, at, &tag, &wire_type);

		const ProtobufCFieldDescriptor *field;
		ScannedMember tmp;

		// Halt iteration due to error
		if (used == 0) { return NULL; }

		// TODO: Consider optimizing for field[1].id == tag, if field[1]
		if (last_field == NULL || last_field->id != tag) {
			// lookup field
			int field_index = int_range_lookup(
				desc->n_field_ranges, desc->field_ranges, tag
			);

			if (field_index < 0) {
				field = NULL;
				n_unknown++;
			}

			else {
				field = desc->fields + field_index;
				last_field = field;
				last_field_index = field_index;
			}

		}
		
		else { field = last_field; }

		if (field != NULL && field->label == PROTOBUF_C_LABEL_REQUIRED) {
			REQUIRED_FIELD_BITMAP_SET(last_field_index);
		}

		at                    += used;
		rem                   -= used;
		tmp.tag                = tag;
		tmp.wire_type          = wire_type;
		tmp.field              = field;
		tmp.data               = at;
		tmp.length_prefix_len  = 0;

		switch (wire_type) {
		case PROTOBUF_C_WIRE_TYPE_VARINT: {
			unsigned max_len = rem < 10 ? rem : 10;
			unsigned i;

			for (i = 0; i < max_len; i++) {
				if ((at[i] & 0x80) == 0) { break; }
			}

			if (i == max_len) {
				PROTOBUF_C_UNPACK_ERROR(
					"unterminated varint at offset %u", (unsigned) (at - data)
				);

				goto error_cleanup_during_scan;
			}

			tmp.len = i + 1;
			break;
		}

		case PROTOBUF_C_WIRE_TYPE_64BIT:
			if (rem < 8) {
				PROTOBUF_C_UNPACK_ERROR(
					"too short after 64bit wiretype at offset %u", (unsigned) (at - data)
				);

				goto error_cleanup_during_scan;
			}

			tmp.len = 8;
			break;

		case PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED: {
			size_t pref_len;

			tmp.len = scan_length_prefixed_data(rem, at, &pref_len);
			if (tmp.len == 0) {
				// NOTE: scan_length_prefixed_data calls UNPACK_ERROR
				goto error_cleanup_during_scan;
			}

			tmp.length_prefix_len = pref_len;
			break;
		}

		case PROTOBUF_C_WIRE_TYPE_32BIT:
			if (rem < 4) {
				PROTOBUF_C_UNPACK_ERROR(
					"too short after 32bit wiretype at offset %u", (unsigned) (at - data)
				);

				goto error_cleanup_during_scan;
			}

			tmp.len = 4;
			break;

		default:
			PROTOBUF_C_UNPACK_ERROR("unsupported tag %u at offset %u",
						wire_type, (unsigned) (at - data));
			goto error_cleanup_during_scan;
		}

		if (in_slab_index == (1UL << (which_slab + FIRST_SCANNED_MEMBER_SLAB_SIZE_LOG2))) {
			size_t size;

			in_slab_index = 0;
			if (which_slab == MAX_SCANNED_MEMBER_SLAB) {
				PROTOBUF_C_UNPACK_ERROR("too many fields");
				goto error_cleanup_during_scan;
			}

			which_slab++;
			size = sizeof(ScannedMember) << (which_slab + FIRST_SCANNED_MEMBER_SLAB_SIZE_LOG2);

			scanned_member_slabs[which_slab] = malloc(size);
			if (scanned_member_slabs[which_slab] == NULL) {
				goto error_cleanup_during_scan;
			}
		}

		scanned_member_slabs[which_slab][in_slab_index++] = tmp;

		if (field != NULL && field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t *n = STRUCT_MEMBER_PTR(size_t, rv, field->quantifier_offset);

			int is_packable_or_packed = (
				0 != (field->flags & PROTOBUF_C_FIELD_FLAG_PACKED) || is_packable_type(field->type)
			);

			int is_wiretype_prefixed = wire_type == PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED;

			if (is_wiretype_prefixed && is_packable_or_packed) {
				size_t count;
				size_t field_bytelen           = tmp.len  - tmp.length_prefix_len;
				const uint8_t *packed_elements = tmp.data + tmp.length_prefix_len;
				if (!count_packed_elements(field->type, field_bytelen, packed_elements, &count))
				{
					PROTOBUF_C_UNPACK_ERROR("counting packed elements");
					goto error_cleanup_during_scan;
				}
				*n += count;
			}
			else {
				*n += 1;
			}
		}

		at += tmp.len;
		rem -= tmp.len;
	}

	// do real parsing
	for (i_slab = 0; i_slab <= which_slab; i_slab++) {
		unsigned max = (i_slab == which_slab) ?
			in_slab_index : (1UL << (i_slab + 4));
		ScannedMember *slab = scanned_member_slabs[i_slab];

		for (j = 0; j < max; j++) {
			if (!parse_member(slab + j, rv, allocator)) {
				PROTOBUF_C_UNPACK_ERROR("error parsing member %s of %s",
							slab->field ? slab->field->name : "*unknown-field*",
					desc->name);
				goto error_cleanup;
			}
		}
	}

	// cleanup
	return rv;
}


// TODO: it's really painful, but this is implemented for functionality first. will remove
// unnecessary allocations later
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
