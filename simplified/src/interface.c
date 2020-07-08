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
#include "types.h"
#include "kinetic.h"

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

// enum BuilderStatus kmessage_initialize(K

/*
 * Requests need only specify the `types` field, except for a "Device" message. If set, the
 * `device` field specifies that only "GetLog" info for that device should be returned. If the name
 * is not found, then the response has status, `NOT_FOUND`.
 */

enum BuilderStatus kheader_initialize(KHeader *const msg_header,
                                      struct KHeaderOptionalFields options) {

    com__seagate__kinetic__proto__command__header__init(msg_header);

    if (options.clusterversion != NULL) {
        msg_header->clusterversion     = *(options.clusterversion);
        msg_header->has_clusterversion = 1;
    }

    if (options.connectionid != NULL) {
        msg_header->connectionid     = *(options.connectionid);
        msg_header->has_connectionid = 1;
    }

    if (options.sequence != NULL) {
        msg_header->sequence     = *(options.sequence);
        msg_header->has_sequence = 1;
    }

    if (options.timeout != NULL) {
        msg_header->timeout     = *(options.timeout);
        msg_header->has_timeout = 1;
    }

    if (options.priority != NULL) {
        msg_header->priority     = *(options.priority);
        msg_header->has_priority = 1;
    }

    if (options.timequanta != NULL) {
        msg_header->timequanta     = *(options.timequanta);
        msg_header->has_timequanta = 1;
    }

    if (options.batchid != NULL) {
        msg_header->batchid     = *(options.batchid);
        msg_header->has_batchid = 1;
    }

    return SUCCESS;
}

enum BuilderStatus kinfo_create_request(KInfo *const info_msg,
                                        ProtobufCBinaryData info_types,
                                        ProtobufCBinaryData *device_name) {

    // Populate `types` field using `info_types` argument.
    info_msg->n_types = info_types.len;
    info_msg->types   = (Com__Seagate__Kinetic__Proto__Command__GetLog__Type *) info_types.data;

    if (device_name != NULL && device_name->len > 0 && device_name->data != NULL) {
        com__seagate__kinetic__proto__command__get_log__device__init(info_msg->device);

        info_msg->device->has_name = 1;
        info_msg->device->name     = *device_name;
    }

    return SUCCESS;
}

struct KineticRequest kinfo_serialize_request(KHeader *const msg_header, KInfo *const info_msg) {
    // Structs to use
    KCommand command_msg;
    KBody    command_body;

    // initialize the structs
    com__seagate__kinetic__proto__command__init(&command_msg);
    com__seagate__kinetic__proto__command__body__init(&command_body);

    // update the header for the GetLog Message Body
    msg_header->messagetype  = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GETLOG;

    // stitch the Command together
    command_msg.header  = msg_header;
    command_msg.body    = &command_body;
    command_body.getlog = info_msg;

    // Get size for command and allocate buffer
    size_t   command_size   = com__seagate__kinetic__proto__command__get_packed_size(&command_msg);
    uint8_t *command_buffer = (uint8_t *) malloc(sizeof(uint8_t) * command_size);

    if (command_buffer == NULL) {
        return (struct KineticRequest) {
            .status = FAILURE,
            .command_bytes = (ProtobufCBinaryData) {
                .len  = 0,
                .data = NULL
            }
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

        return (struct KineticRequest) {
            .status = FAILURE,
            .command_bytes = (ProtobufCBinaryData) {
                .len  = 0,
                .data = NULL
            }
        };
    }

    return (struct KineticRequest) {
        .status        = SUCCESS,
        .command_bytes = (ProtobufCBinaryData) {
            .len  = command_size,
            .data = command_buffer
        }
    };
}

struct KineticResponse kinfo_deserialize_response(ProtobufCBinaryData serialized_msg) {
    KCommand *command_response = com__seagate__kinetic__proto__command__unpack(
        NULL,
        serialized_msg.len,
        serialized_msg.data
    );

    KInfo *info_response = command_response->body->getlog;

    if (info_response == NULL) {
        return (struct KineticResponse) {
            .status           = FAILURE,
            .command_response = NULL
        };
    }

    return (struct KineticResponse) {
        .status           = SUCCESS,
        .command_response = command_response
    };
}

/*
 *
    KineticResponse * response = KineticAllocator_NewKineticResponse(si->header.valueLength);

    if (response == NULL) {
        bus_unpack_cb_res_t res = {
            .ok = false,
            .u.error.opaque_error_id = UNPACK_ERROR_PAYLOAD_MALLOC_FAIL,
        };
        return res;
    } else {
        response->header = si->header;

        response->proto = KineticPDU_unpack_message(NULL, si->header.protobufLength, si->buf);

        if (response->header.valueLength > 0)
        {
            memcpy(response->value, &si->buf[si->header.protobufLength], si->header.valueLength);
        }

        int64_t seq_id = BUS_NO_SEQ_ID;
        if (response->command != NULL &&
            response->command->header != NULL)
        {
            if (response->proto->has_authtype &&
                response->proto->authtype == COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__UNSOLICITEDSTATUS
                && KineticSession_GetConnectionID(session) == 0)
            {
                // Ignore the unsolicited status message on connect.
                seq_id = BUS_NO_SEQ_ID;
            } else {
                seq_id = response->command->header->acksequence;
            }
            log_response_seq_id(session->socket, seq_id);
        }

        bus_unpack_cb_res_t res = {
            .ok = true,
            .u.success = {
                .seq_id = seq_id,
                .msg = response,
            },
        };
        return res;
    }
*/

/*
 *
KineticLogInfo* KineticLogInfo_Create(const Com__Seagate__Kinetic__Proto__Command__GetLog* getLog)
{
    KINETIC_ASSERT(getLog != NULL);

    // Copy data into the nested allocated structure tree
    KineticLogInfo* info = calloc(1, sizeof(*info));
    if (info == NULL) { return NULL; }
    memset(info, 0, sizeof(*info));

    KineticLogInfo_Utilization* utilizations = NULL;
    KineticLogInfo_Temperature* temperatures = NULL;
    KineticLogInfo_Capacity* capacity = NULL;
    KineticLogInfo_Configuration* configuration = NULL;
    KineticLogInfo_Statistics* statistics = NULL;
    KineticLogInfo_Limits* limits = NULL;
    KineticLogInfo_Device* device = NULL;

    utilizations = KineticLogInfo_GetUtilizations(getLog, &info->numUtilizations);
    if (utilizations == NULL) { goto cleanup; }
    capacity = KineticLogInfo_GetCapacity(getLog);
    if (capacity == NULL) { goto cleanup; }
    temperatures = KineticLogInfo_GetTemperatures(getLog, &info->numTemperatures);
    if (temperatures == NULL) { goto cleanup; }

    if (getLog->configuration != NULL) {
        configuration = KineticLogInfo_GetConfiguration(getLog);
        if (configuration == NULL) { goto cleanup; }
    }

    statistics = KineticLogInfo_GetStatistics(getLog, &info->numStatistics);
    if (statistics == NULL) { goto cleanup; }
    ByteArray messages = KineticLogInfo_GetMessages(getLog);
    if (messages.data == NULL) { goto cleanup; }

    if (getLog->limits != NULL) {
        limits = KineticLogInfo_GetLimits(getLog);
        if (limits == NULL) { goto cleanup; }
    }

    device = KineticLogInfo_GetDevice(getLog);
    if (device == NULL) { goto cleanup; }

    info->utilizations = utilizations;
    info->temperatures = temperatures;
    info->capacity = capacity;
    info->configuration = configuration;
    info->statistics = statistics;
    info->limits = limits;
    info->device = device;
    info->messages = messages;

    LOGF2("Created KineticLogInfo @ 0x%0llX", info);
    return info;

cleanup:
    if (info) { free(info); }
    if (utilizations) { free(utilizations); }
    if (temperatures) { free(temperatures); }
    if (capacity) { free(capacity); }
    if (configuration) { free(configuration); }
    if (statistics) { free(statistics); }
    if (limits) { free(limits); }
    if (device) { free(device); }
    if (messages.data) { free(messages.data); }
    return NULL;
}
*/
