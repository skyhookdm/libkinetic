/**
 * Copyright 2020-2021 Seagate Technology LLC.
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
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"
#include "protocol_interface.h"

#include "githash.h"

#define PB_KINETIC_VERS \
	com__seagate__kinetic__proto__local__protocol_version__default_value

#define KVERS_MAJOR 0
#define KVERS_MINOR 3
#define KVERS_PATCH 0

static char     v_kinetic_vers[40];
static uint32_t v_kinetic_vers_num = (
	  (KVERS_MAJOR * 1E6)
	+ (KVERS_MINOR * 1E3)
	+ (KVERS_PATCH      )
);

extern const char *protobuf_c_version(void);
extern uint32_t    protobuf_c_version_number(void);

kstatus_t
ki_version(kversion_t *kver)
{
	if (!ki_valid(kver)) {
		return (K_EINVAL);
	}

	sprintf(v_kinetic_vers
		,"%d.%d.%d"
		,KVERS_MAJOR, KVERS_MINOR, KVERS_PATCH
    );

	kver->kvn_pb_c_vers       = protobuf_c_version();
	kver->kvn_pb_c_vers_num   = protobuf_c_version_number();
	kver->kvn_pb_kinetic_vers = PB_KINETIC_VERS;
	kver->kvn_ki_githash      = ki_githash;
	kver->kvn_ki_vers         = v_kinetic_vers;
	kver->kvn_ki_vers_num     = v_kinetic_vers_num;

	return (K_OK);
}
