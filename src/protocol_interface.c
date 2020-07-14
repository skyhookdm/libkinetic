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

typedef Com__Seagate__Kinetic__Proto__Message__HMACauth kauth_hmac;
typedef Com__Seagate__Kinetic__Proto__Message__PINauth  kauth_pin;

struct kresult_message create_header(uint8_t header_fields_bitmap, ...) {
    int     num_fields = 0;
    va_list header_field_values;

    // count how many bits are set
    for (int bit_pos = 0; bit_pos < 7; bit_pos++) {
        if (header_fields_bitmap & (1 << bit_pos)) { num_fields++; }
    }

    va_start(header_field_values, header_fields_bitmap);

    // allocate and populate message header
    kcmd_hdr_t *cmd_hdr = (kcmd_hdr_t *) malloc(sizeof(kcmd_hdr_t));
    com__seagate__kinetic__proto__command__header__init(cmd_hdr);

    if (header_fields_bitmap & CLUST_VER) {
        cmd_hdr->clusterversion     = va_arg(header_field_values, int64_t);
        cmd_hdr->has_clusterversion = 1;
    }

    if (header_fields_bitmap & CONN_ID) {
        cmd_hdr->connectionid     = va_arg(header_field_values, int64_t);
        cmd_hdr->has_connectionid = 1;
    }

    if (header_fields_bitmap & SEQ_NUM) {
        cmd_hdr->sequence     = va_arg(header_field_values, uint64_t);
        cmd_hdr->has_sequence = 1;
    }

    if (header_fields_bitmap & TIMEOUT) {
        cmd_hdr->timeout     = va_arg(header_field_values, uint64_t);
        cmd_hdr->has_timeout = 1;
    }

    if (header_fields_bitmap & PRIORITY) {
        cmd_hdr->priority     = va_arg(header_field_values, kproto_priority);
        cmd_hdr->has_priority = 1;
    }

    if (header_fields_bitmap & TIME_QUANTA) {
        cmd_hdr->timequanta     = va_arg(header_field_values, uint64_t);
        cmd_hdr->has_timequanta = 1;
    }

    if (header_fields_bitmap & BATCH_ID) {
        cmd_hdr->batchid     = va_arg(header_field_values, uint32_t);
        cmd_hdr->has_batchid = 1;
    }

    va_end(header_field_values);
    return (struct kresult_message) {
        .result_code    = SUCCESS,
        .result_message = (void *) cmd_hdr
    };
}

struct kresult_message unpack_response(struct kbuffer response_buffer) {
    kcmd_t *response_command = com__seagate__kinetic__proto__command__unpack(
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

ProtobufCBinaryData pack_kinetic_command(kcmd_t *cmd_data) {
    // Get size for command and allocate buffer
    size_t   command_size   = com__seagate__kinetic__proto__command__get_packed_size(cmd_data);
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

struct kresult_buffer pack_kinetic_message(kmsg_t *msg_data) {
    // Get size for command and allocate buffer
    size_t   msg_size   = com__seagate__kinetic__proto__message__get_packed_size(msg_data);
    uint8_t *msg_buffer = (uint8_t *) malloc(sizeof(uint8_t) * msg_size);

    if (msg_buffer == NULL) { goto pack_failure; }

    size_t num_bytes_packed = com__seagate__kinetic__proto__message__pack(msg_data, msg_buffer);

    if (num_bytes_packed != msg_size) {
        fprintf(
            stderr,
            "Unexpected amount of bytes packed. %ld bytes packed, expected %ld\n",
            num_bytes_packed,
            msg_size
        );

        goto pack_failure;
    }

    return (struct kresult_buffer) {
        .result_code = SUCCESS,
        .len         = num_bytes_packed,
        .base        = msg_buffer,
    };

pack_failure:
    return (struct kresult_buffer) {
        .result_code = FAILURE,
        .len         = 0,
        .base        = NULL
    };
}

struct kresult_message create_message(kmsg_auth_t *msg_auth, ProtobufCBinaryData cmd_bytes) {
    kmsg_t *kinetic_msg = (kmsg_t *) malloc(sizeof(kmsg_t));
    if (kinetic_msg == NULL) {
        goto create_failure;
    }

    com__seagate__kinetic__proto__message__init(kinetic_msg);

    if (cmd_bytes.len && cmd_bytes.data != NULL) {
        kinetic_msg->has_commandbytes = 1;
        kinetic_msg->commandbytes     = cmd_bytes;
    }

    switch(msg_auth->auth_type) {
        case MAT_HMAC:
            kinetic_msg->has_authtype = 1;
            kinetic_msg->authtype     = MAT_HMAC;

            kauth_hmac *msg_auth_hmac = (kauth_hmac *) malloc(sizeof(kauth_hmac));
            com__seagate__kinetic__proto__message__hmacauth__init(msg_auth_hmac);

            msg_auth_hmac->has_identity = 1;
            msg_auth_hmac->identity     = msg_auth->hmac_identity;

            msg_auth_hmac->has_hmac = 1;
            msg_auth_hmac->hmac     = (ProtobufCBinaryData) {
                .len  = msg_auth->auth_len,
                .data = msg_auth->auth_data
            };

            kinetic_msg->hmacauth = msg_auth_hmac;

            break;

        case MAT_PIN:
            kinetic_msg->has_authtype = 1;
            kinetic_msg->authtype     = MAT_PIN;

            kauth_pin *msg_auth_pin = (kauth_pin *) malloc(sizeof(kauth_pin));
            com__seagate__kinetic__proto__message__pinauth__init(msg_auth_pin);

            msg_auth_pin->has_pin = 1;
            msg_auth_pin->pin     = (ProtobufCBinaryData) {
                .len  = msg_auth->auth_len,
                .data = msg_auth->auth_data
            };

            kinetic_msg->pinauth = msg_auth_pin;
            break;

        case MAT_INVALID:
        default:
            goto create_failure;
    };

    return (struct kresult_message) {
        .result_code    = SUCCESS,
        .result_message = kinetic_msg,
    };

create_failure:
    return (struct kresult_message) {
        .result_code    = FAILURE,
        .result_message = NULL,
    };
}
