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

#ifndef __KINETIC_TYPES_H
#define __KINETIC_TYPES_H


#include "protocol/kinetic.pb-c.h"

/**
 * This header defines structs for the kinetic library, and functions for the structs defined.
 *
 * Typically, once would want to implement these and provide mappings to the generated wire
 * protocol code. This library actually provides a thin wrapper around the generated protobuf code,
 * as these structures should serve perfectly well for passing data around.
 */

/* ------------------------------
 * Macros
 */

#define HEADER_FIELD_COUNT 7


/* ------------------------------
 * Aliases for Message types
 */
typedef Com__Seagate__Kinetic__Proto__Message                                   kproto_message;
typedef Com__Seagate__Kinetic__Proto__Message__HMACauth                         kproto_hmac;
typedef Com__Seagate__Kinetic__Proto__Message__PINauth                          kproto_pin;

typedef Com__Seagate__Kinetic__Proto__Command                                   kproto_command;
typedef Com__Seagate__Kinetic__Proto__Command__Header                           kproto_header;
typedef Com__Seagate__Kinetic__Proto__Command__Body                             kproto_body;
typedef Com__Seagate__Kinetic__Proto__Command__Status                           kproto_status;

// Sub-message of a Command Body for device information and state
typedef Com__Seagate__Kinetic__Proto__Command__GetLog                           kproto_getlog;


/* ------------------------------
 * Aliases for Field types
 */

/* Aliases for structs */

// Field types for requests
typedef Com__Seagate__Kinetic__Proto__Command__Priority                         kproto_priority;

// Field types for responses
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Utilization              kproto_utilization;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Temperature              kproto_temperature;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Capacity                 kproto_capacity;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration            kproto_configuration;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration__Interface kproto_interface_config;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Statistics               kproto_statistics;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Device                   kproto_device_info;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Limits                   kproto_device_limits;


/* Aliases for enums */
typedef Com__Seagate__Kinetic__Proto__Command__MessageType                      kproto_message_type;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Type                     kproto_getlog_type;

// Authorization types (`AuthType`)
typedef enum {
  INVALID_AUTH_TYPE     = COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__INVALID_AUTH_TYPE,
  HMAC_AUTH_TYPE        = COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__HMACAUTH         ,
  PIN_AUTH_TYPE         = COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__PINAUTH          ,
  UNSOLICITED_AUTH_TYPE = COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__UNSOLICITEDSTATUS,
} kproto_auth_type;


// Message types (`MessageType`)
typedef enum {
    INVALID_MSG_TYPE     = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__INVALID_MESSAGE_TYPE,
    GET_MSG_TYPE         = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GET                 ,
    PUT_MSG_TYPE         = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__PUT                 ,
    DELETE_MSG_TYPE      = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__DELETE              ,
    GET_NEXT_MSG_TYPE    = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GETNEXT             ,
    GET_PREV_MSG_TYPE    = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GETPREVIOUS         ,
    GET_RANGE_MSG_TYPE   = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GETKEYRANGE         ,
    GET_VER_MSG_TYPE     = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GETVERSION          ,
    SETUP_MSG_TYPE       = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__SETUP               ,
    GETLOG_MSG_TYPE      = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__GETLOG              ,
    SEC_MSG_TYPE         = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__SECURITY            ,
    PUSH_MSG_TYPE        = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__PEER2PEERPUSH       ,
    NOOP_MSG_TYPE        = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__NOOP                ,
    FLUSH_MSG_TYPE       = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__FLUSHALLDATA        ,
    PIN_MSG_TYPE         = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__PINOP               ,
    SCAN_MSG_TYPE        = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__MEDIASCAN           ,
    OPTIMIZE_MSG_TYPE    = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__MEDIAOPTIMIZE       ,
    START_BATCH_MSG_TYPE = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__START_BATCH         ,
    END_BATCH_MSG_TYPE   = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__END_BATCH           ,
    ABORT_BATCH_MSG_TYPE = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__ABORT_BATCH         ,
    SET_POWER_MSG_TYPE   = COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__SET_POWER_LEVEL     ,
} kproto_msg_type;


// Types of information for GetLog (`Type`). Synonymous with kproto_getlog_type.
typedef enum {
    INVALID_INFO_TYPE          = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__INVALID_TYPE ,
    UTIL_INFO_TYPE             = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__UTILIZATIONS ,
    TEMP_INFO_TYPE             = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__TEMPERATURES ,
    CAPACITY_INFO_TYPE         = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__CAPACITIES   ,
    CONFIG_INFO_TYPE           = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__CONFIGURATION,
    INTERFACE_CONFIG_INFO_TYPE = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__STATISTICS   ,
    MESSAGE_INFO_TYPE          = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__MESSAGES     ,
    DEVICE_LIMITS_INFO_TYPE    = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__LIMITS       ,
    DEVICE_INFO_TYPE           = COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__DEVICE       ,
} kproto_info_type;



/* ------------------------------
 * Custom types
 */

// Types for interfacing with protobufs
enum header_field_type {
    CLUST_VER    = (uint8_t) 1 << 0,
    CONN_ID      = (uint8_t) 1 << 1,
    SEQ_NUM      = (uint8_t) 1 << 2,
    TIMEOUT      = (uint8_t) 1 << 3,
    PRIORITY     = (uint8_t) 1 << 4,
    TIME_QUANTA  = (uint8_t) 1 << 5,
    BATCH_ID     = (uint8_t) 1 << 6,
};

// Types for interfacing with API
enum kresult_code {
    SUCCESS = 0,
    FAILURE    ,
};

struct kbuffer {
    size_t  len;
    void   *base;
};

struct kresult_buffer {
    enum kresult_code  result_code;
    size_t             len;
    void              *base;
};

struct kresult_message {
    enum kresult_code  result_code;
    void              *result_message;
};


/** ktli session helper functions and data.
 * 
 * These provide enough session info to abstract the ktli layer from the the kinetic protocol
 * structure. To accomplish this, a session must be preconfigured with:
 *  - (data)     length of response header (full message length extracted from header)
 *  - (function) logic to set the seqence number in an outbound request
 *  - (function) logic to extract the ackSequence number from an inbound response
 *  - (function) logic to return the expected response length (given a header buffer)
 */
struct kiovec;
struct ktli_helpers {
    int     kh_recvhdr_len;         

    // functions
    int64_t (*kh_getaseq_fn)(struct kiovec *msg, int msgcnt);
    void    (*kh_setseq_fn)(struct kiovec *msg, int msgcnt, int64_t seq);
    int32_t (*kh_msglen_fn)(struct kiovec *msg_hdr);
};

#endif //__KINETIC_TYPES_H
