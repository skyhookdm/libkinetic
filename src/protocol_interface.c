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
