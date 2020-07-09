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

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include "protocol/kinetic.pb-c.h"


/* ------------------------------
 * Function prototypes
 */

struct kresult_message create_header(uint8_t header_fields_bitmap, ...);

struct kresult_message create_info_request(struct kbuffer  info_types,
                                           struct kbuffer *device_name);

struct kresult_buffer  pack_info_request(kproto_header *const msg_header, kproto_getlog *const info_msg);
struct kresult_message unpack_info_response(struct kbuffer response_buffer);


#endif //__KINETIC_H
