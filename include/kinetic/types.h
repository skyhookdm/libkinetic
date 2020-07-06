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


#include <stdlib.h>
#include "protocol/kinetic.pb-c.h"

/**
 * This header defines structs for the kinetic library, and functions for the structs defined.
 *
 * Typically, once would want to implement these and provide mappings to the generated wire
 * protocol code. This library actually provides a thin wrapper around the generated protobuf code,
 * as these structures should serve perfectly well for passing data around.
 */


/* ------------------------------
 * Aliases for Message types
 */
typedef Com__Seagate__Kinetic__Proto__Message                                   KMessage;
typedef Com__Seagate__Kinetic__Proto__Message__HMACauth                         KAuthHmac;
typedef Com__Seagate__Kinetic__Proto__Message__PINauth                          KAuthPin;

typedef Com__Seagate__Kinetic__Proto__Command                                   KCommand;
typedef Com__Seagate__Kinetic__Proto__Command__Header                           KHeader;
typedef Com__Seagate__Kinetic__Proto__Command__Body                             KBody;
typedef Com__Seagate__Kinetic__Proto__Command__Status                           KStatus;

// Sub-message of a Command Body for device information and state
typedef Com__Seagate__Kinetic__Proto__Command__GetLog                           KInfo;

/* ------------------------------
 * Aliases for Field types
 */

typedef Com__Seagate__Kinetic__Proto__Command__Priority                         KPriority;

// Field types for requests
typedef Com__Seagate__Kinetic__Proto__Message__AuthType                         KAuthType;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Type                     KInfoType;

// Field types for responses
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Utilization              KUtilization;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Temperature              KTemperature;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Capacity                 KCapacity;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration            KConfiguration;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration__Interface KConfig_interface;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Statistics               KStatistics;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Device                   KDeviceInfo;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Limits                   KDeviceLimits;


/* ------------------------------
 * Extra definitions to make the interface more usable */

enum BuilderStatus {
    SUCCESS = 0,
    FAILURE = 1
};

struct CommandResult {
    enum BuilderStatus  status;
    ProtobufCBinaryData command_bytes;
};

/* This is meant to be a convenient way of specifying values for optional fields:
 *  - The attributes are human readable (easily mappable to the protobuf definition)
 *  - Each attribute is a pointer, so that it is easy to pass an "empty" value (NULL)
 *  - It is a struct specific to a Message type, so that behavior is predictable
 */
struct KHeaderOptionalFields {
     int64_t  *clusterversion;
     int64_t  *connectionid;
    uint64_t  *sequence;
    uint64_t  *timeout;
    KPriority *priority;
    uint64_t  *timequanta;
    uint32_t  *batchid;
};


/* ------------------------------
 * Function prototypes
 */

enum BuilderStatus kheader_initialize(KHeader *const msg_header,
                                      struct KHeaderOptionalFields options);

enum BuilderStatus kinfo_create_request(KInfo *const info_msg,
                                        ProtobufCBinaryData  info_types,
                                        ProtobufCBinaryData *device_name);

struct CommandResult kinfo_serialize_request(KCommand *const command_msg,
                                             KHeader  *const msg_header,
                                             KInfo    *const info_msg);


#endif //__KINETIC_TYPES_H
