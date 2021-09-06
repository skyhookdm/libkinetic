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

/*
 * This is the public routine for doing device operations. They include
 * the pinops as well as scan and optimize. The latter two are as yet
 * unimplemented.  But this routine multiplexes the req to the right handler
 * Right now all allowed ops go to ki_pinop.
 */
kstatus_t
ki_device(int ktd, void *pin, size_t pinlen, kdevop_t op)
{
	switch (op) {
	case KDO_UNLOCK:
	case KDO_LOCK:
	case KDO_ERASE:
	case KDO_SECURE_ERASE:
		return(ki_pinop(ktd, pin, pinlen, op));

#if 0
	case KDO_SCAN:
		/* TODO */
		break;

	case KDO_OPTIMIZE:
		/* TODO */
		break;
#endif

	default:
		return(K_EINTERNAL);
	}
}
