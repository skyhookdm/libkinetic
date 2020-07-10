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

#include "protocol_types.h"
#include "protocol_interface.h"
#include "getlog.h"

struct kresult_message create_getlog_request(struct kbuffer  getlog_types_buffer,
                                             struct kbuffer *device_name) {

    kproto_getlog *getlog_msg = (kproto_getlog *) malloc(sizeof(kproto_getlog));
    com__seagate__kinetic__proto__command__get_log__init(getlog_msg);

    // Populate `types` field using `getlog_types_buffer` argument.
    getlog_msg->n_types = getlog_types_buffer.len;
    getlog_msg->types   = (kproto_getlog_type *) getlog_types_buffer.base;

    if (device_name != NULL && device_name->len > 0 && device_name->base != NULL) {
        kproto_device_info *getlog_msg_device = (kproto_device_info *) malloc(sizeof(kproto_device_info));
        com__seagate__kinetic__proto__command__get_log__device__init(getlog_msg_device);

        getlog_msg_device->has_name = 1;
        getlog_msg_device->name     = (ProtobufCBinaryData) {
            .len  = device_name->len,
            .data = (uint8_t *) device_name->base
        };

        getlog_msg->device = getlog_msg_device;
    }

    return (struct kresult_message) {
        .result_code    = SUCCESS,
        .result_message = (void *) getlog_msg
    };
}

struct kresult_buffer pack_getlog_request(kproto_header *const msg_header,
                                          kproto_getlog *const getlog_msg) {
    // Structs to use
    kproto_command command_msg;
    kproto_body    command_body;

    // initialize the structs
    com__seagate__kinetic__proto__command__init(&command_msg);
    com__seagate__kinetic__proto__command__body__init(&command_body);

    // update the header for the GetLog Message Body
    msg_header->messagetype = GETLOG_MSG_TYPE;

    // stitch the Command together
    command_body.getlog = getlog_msg;

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
