/**
 * Copyright 2021-2021 Seagate Technology LLC.
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
#include "kinetic.h"

static const char *k_status_invalid = "Invalid Kinitic Status";

static const char *k_status_grp1[] = {
	/* 0x0000  0 EREJECTED	*/ "Not attempted",
	/* 0x0001  1 OK		*/ "Success",
	/* 0x0002  2 EHMAC	*/ "HMAC failure",
	/* 0x0003  3 EACCESS	*/ "Not authorized",
	/* 0x0004  4 EVERSION	*/ "Version failure",
	/* 0x0005  5 EINTERNAL	*/ "Internal error",
	/* 0x0006  6 ENOHEADER	*/ "Header required",
	/* 0x0007  7 ENOTFOUND	*/ "Not found",
	/* 0x0008  8 EBADVERS	*/ "Version mismatch",
	/* 0x0009  9 EBUSY	*/ "Service busy",
	/* 0x000a 10 ETIMEDOUT	*/ "Expired",
	/* 0x000b 11 EDATA	*/ "Data error",
	/* 0x000c 12 EPERMDATA	*/ "Perm data error",
	/* 0x000d 13 EP2PCONN	*/ "Remote connection error",
	/* 0x000e 14 ENOSPACE	*/ "No space",
	/* 0x000f 15 ENOHMAC	*/ "No such hmac algorithm",
	/* 0x0010 16 EINVAL	*/ "Invalid request",
	/* 0x0011 17 EP2P	*/ "Nested operation errors",
	/* 0x0012 18 ELOCKED	*/ "Device locked",
	/* 0x0013 19 ENOTLOCKED	*/ "Device already unlocked",
	/* 0x0014 20 ECONNABORTED */ "Connection terminated",
	/* 0x0015 21 EINVALBAT	*/ "Invalid batch",
	/* 0x0016 22 EHIBERNATE	*/ "Hibernate",
	/* 0x0017 23 ESHUTDOWN	*/ "Shutdown",
};

static const char *k_status_grp2[] = {
	/* 0x8000  0 EAGAIN	*/ "",
	/* 0x8000  0 ENOMSG	*/ "Unable to allocate memory",
	/* 0x8000  0 ENOMEM	*/ "Bad Session",
	/* 0x8000  0 EBADSESS	*/ "Invalid Argument(s)",
	/* 0x8000  0 EMSGPACK	*/ "Unable to pack kinetic message",
	/* 0x8000  0 EMSGUNPACK	*/ "Unable to unpack kinetic message",
	/* 0x8000  0 ECMDUNPACK	*/ "Unable to unpack kinetic command",
	/* 0x8000  0 ENOCMD	*/ "Message has no command data",
	/* 0x8000  0 ECREATEREQ	*/ "Unable to construct kinetic request",
	/* 0x8000  0 ERECVMSG	*/ "Failed to receive message",
	/* 0x8000  0 ERECVPDU	*/ "Unexpected PDU length",
	/* 0x8000  0 EPDUMSGLEN	*/ "Length mismatch: PDU != message + value",
	/* 0x8000  0 EBATCH	*/ "General Batch Error",
};

const char *
ki_error(kstatus_t ks)
{

	if ((ks >= KSTAT_GRP1) && (ks < KSTAT_GRP1_LAST))
		return(k_status_grp1[(ks ^ KSTAT_GRP1)]);

	if ((ks >= KSTAT_GRP2) && (ks < KSTAT_GRP2_LAST))
		return(k_status_grp2[(ks ^ KSTAT_GRP2)]);

	return(k_status_invalid);
}
