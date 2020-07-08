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

#ifndef __KINETIC_H
#define __KINETIC_H

#include <stdlib.h>
#include "protocol/kinetic.pb-c.h"

/* ------------------------------
 * Function prototypes
 */

enum BuilderStatus kheader_initialize(KHeader *const msg_header,
                                      struct KHeaderOptionalFields options);

enum BuilderStatus kinfo_create_request(KInfo *const info_msg,
                                        ProtobufCBinaryData  info_types,
                                        ProtobufCBinaryData *device_name);

struct KineticRequest kinfo_serialize_request(KHeader *const msg_header, KInfo *const info_msg);
struct KineticResponse kinfo_deserialize_response(ProtobufCBinaryData serialized_msg);



#endif //__KINETIC_H
