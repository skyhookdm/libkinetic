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
#include <stdlib.h>

#include "getlog.h"


// Typedefs for convenience


/*
 * Helper functions
 */
//TODO: test
ProtobufCBinaryData pack_cmd_getlog(kcmd_hdr_t *cmd_hdr, kcmd_getlog_t *cmd_getlog) {
    // Structs to use
    kcmd_t      command_msg;
    kcmd_body_t command_body;

    // initialize the structs
    com__seagate__kinetic__proto__command__init(&command_msg);
    com__seagate__kinetic__proto__command__body__init(&command_body);

    // update the header for the GetLog Message Body
    cmd_hdr->messagetype = KMT_GETLOG;

    // stitch the Command together
    command_body.getlog = cmd_getlog;

    command_msg.header  = cmd_hdr;
    command_msg.body    = &command_body;

    return pack_kinetic_command(&command_msg);
}

kcmd_getlog_t *to_command(kgetlog_t *cmd_data) {
    kcmd_getlog_t *getlog_msg = (kcmd_getlog_t *) malloc(sizeof(kcmd_getlog_t));
    com__seagate__kinetic__proto__command__get_log__init(getlog_msg);

    // Populate `types` field using `getlog_types_buffer` argument.
    getlog_msg->n_types = cmd_data->kgl_typecnt;
    getlog_msg->types   = (kgltype_t *) cmd_data->kgl_type;

    // Should device name have a length attribute?
    if (cmd_data->kgl_log.kdl_name != NULL) {
        kgetlog_device_info *getlog_msg_device = (kgetlog_device_info *) malloc(sizeof(kgetlog_device_info));
        com__seagate__kinetic__proto__command__get_log__device__init(getlog_msg_device);

        getlog_msg_device->has_name = 1;
        getlog_msg_device->name     = (ProtobufCBinaryData) {
            .len  =             cmd_data->kgl_log.len,
            .data = (uint8_t *) cmd_data->kgl_log.kdl_name
        };

        getlog_msg->device = getlog_msg_device;
    }

    return getlog_msg;
}


/*
 * Externally accessible functions
 */
// TODO: test
struct kresult_message create_getlog_message(kmsg_auth_t *msg_auth, kcmd_hdr_t *cmd_hdr, kgetlog_t *cmd_body) {

    // create and pack the Command
    kcmd_getlog_t       *cmd_body_getlog = to_command(cmd_body);
    ProtobufCBinaryData  command_bytes   = pack_cmd_getlog(cmd_hdr, cmd_body_getlog);

    // return the constructed getlog message (or failure)
    return create_message(msg_auth, command_bytes);
}
