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
#include "protocol_types.h"
#include "protocol_interface.h"

/**
 * This file holds the highest-level of the implementation of an interface to the protocol. We want
 * this to be as general as possible so that any backend can use the protocol, but we also want to
 * provide a reasonable default. The surface of the protocol interface is as follows:
 *
 * Message building for requests:
 *  - Each message must be built specially, but we hope to wrap them in a "narrow waist" of
 *    constructor operators
 *  - Each message may have fields that are only for the request, and not for the response, and
 *    vice versa. We want to highlight what the request-only fields are, to make clear what
 *    lifetime these values must have.
 *
 * Message decoding for responses:
 *
 *
 * Extra, overall notes:
 *  - The unsolicited message at the beginning of a connection will return the drive status.
 *  - Proprietary names should be prefaced by the vendor name (e.g. "com.WD" for Western Digital
 *    devices).
 *  - The limit of each log is 1m byte.
 */


// ------------------------------
// Message building for requests

// enum kproto_result_code kmessage_initialize(K

/*
 * Requests need only specify the `types` field, except for a "Device" message. If set, the
 * `device` field specifies that only "GetLog" info for that device should be returned. If the name
 * is not found, then the response has status, `NOT_FOUND`.
 */

struct kresult_message create_header(uint8_t header_fields_bitmap, ...) {
    int            num_fields = 0;
    va_list        header_field_values;

    // count how many bits are set
    for (int bit_pos = 0; bit_pos < 7; bit_pos++) {
        if (header_fields_bitmap & (1 << bit_pos)) { num_fields++; }
    }

    va_start(header_field_values, header_fields_bitmap);

    // allocate and populate message header
    kproto_header *msg_header = (kproto_header *) malloc(sizeof(kproto_header));
    com__seagate__kinetic__proto__command__header__init(msg_header);

    if (header_fields_bitmap & CLUST_VER) {
        msg_header->clusterversion     = va_arg(header_field_values, int64_t);
        msg_header->has_clusterversion = 1;
    }

    if (header_fields_bitmap & CONN_ID) {
        msg_header->connectionid     = va_arg(header_field_values, int64_t);
        msg_header->has_connectionid = 1;
    }

    if (header_fields_bitmap & SEQ_NUM) {
        msg_header->sequence     = va_arg(header_field_values, uint64_t);
        msg_header->has_sequence = 1;
    }

    if (header_fields_bitmap & TIMEOUT) {
        msg_header->timeout     = va_arg(header_field_values, uint64_t);
        msg_header->has_timeout = 1;
    }

    if (header_fields_bitmap & PRIORITY) {
        msg_header->priority     = va_arg(header_field_values, kproto_priority);
        msg_header->has_priority = 1;
    }

    if (header_fields_bitmap & TIME_QUANTA) {
        msg_header->timequanta     = va_arg(header_field_values, uint64_t);
        msg_header->has_timequanta = 1;
    }

    if (header_fields_bitmap & BATCH_ID) {
        msg_header->batchid     = va_arg(header_field_values, uint32_t);
        msg_header->has_batchid = 1;
    }

    va_end(header_field_values);
    return (struct kresult_message) {
        .result_code    = SUCCESS,
        .result_message = (void *) msg_header
    };
}

struct kresult_message create_info_request(struct kbuffer  info_types_buffer,
                                           struct kbuffer *device_name) {

    kproto_getlog *info_msg = (kproto_getlog *) malloc(sizeof(kproto_getlog));
    com__seagate__kinetic__proto__command__get_log__init(info_msg);

    // Populate `types` field using `info_types_buffer` argument.
    info_msg->n_types = info_types_buffer.len;
    info_msg->types   = (kproto_getlog_type *) info_types_buffer.base;

    if (device_name != NULL && device_name->len > 0 && device_name->base != NULL) {
        kproto_device_info *info_msg_device = (kproto_device_info *) malloc(sizeof(kproto_device_info));
        com__seagate__kinetic__proto__command__get_log__device__init(info_msg_device);

        info_msg_device->has_name = 1;
        info_msg_device->name     = (ProtobufCBinaryData) {
            .len  = device_name->len,
            .data = (uint8_t *) device_name->base
        };

        info_msg->device = info_msg_device;
    }

    return (struct kresult_message) {
        .result_code    = SUCCESS,
        .result_message = (void *) info_msg
    };
}

struct kresult_buffer pack_info_request(kproto_header *const msg_header, kproto_getlog *const info_msg) {
    // Structs to use
    kproto_command command_msg;
    kproto_body    command_body;

    // initialize the structs
    com__seagate__kinetic__proto__command__init(&command_msg);
    com__seagate__kinetic__proto__command__body__init(&command_body);

    // update the header for the GetLog Message Body
    msg_header->messagetype = GETLOG_MSG_TYPE;

    // stitch the Command together
    command_body.getlog = info_msg;

    command_msg.header  = msg_header;
    command_msg.body    = &command_body;

    // Get size for command and allocate buffer
    size_t   command_size   = com__seagate__kinetic__proto__command__get_packed_size(&command_msg);
    uint8_t *command_buffer = (uint8_t *) malloc(sizeof(uint8_t) * command_size);

    if (command_buffer == NULL) {
        return (struct kresult_buffer) {
            .result_code = FAILURE,
            .len         = 0,
            .base        = NULL
        };
    }

    size_t packed_bytes = com__seagate__kinetic__proto__command__pack(&command_msg, command_buffer);

    if (packed_bytes != command_size) {
        fprintf(
            stderr,
            "Unexpected amount of bytes packed. %ld bytes packed, expected %ld\n",
            packed_bytes,
            command_size
        );

        return (struct kresult_buffer) {
            .result_code = FAILURE,
            .len         = 0,
            .base        = NULL
        };
    }

    return (struct kresult_buffer) {
        .result_code = SUCCESS,
        .len         = packed_bytes,
        .base        = (void *) command_buffer
    };
}

struct kresult_message unpack_response(struct kbuffer response_buffer) {
    kproto_command *response_command = com__seagate__kinetic__proto__command__unpack(
        NULL,
        response_buffer.len,
        (uint8_t *) response_buffer.base
    );

    int unpacked_result_is_invalid = (
           response_command               == NULL
        || response_command->body         == NULL
        || response_command->body->getlog == NULL
    );

    if (unpacked_result_is_invalid) {
        return (struct kresult_message) {
            .result_code    = FAILURE,
            .result_message = NULL
        };
    }

    return (struct kresult_message) {
        .result_code    = SUCCESS,
        .result_message = (void *) response_command
    };
}
