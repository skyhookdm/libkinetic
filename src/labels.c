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

char *ki_msgtype_label[] = {
	/* 0x00  0 */ "Reserved Message Type 0",
	/* 0x01  1 */ "Get Resp",
	/* 0x02  2 */ "Get",
	/* 0x03  3 */ "Put Resp",
	/* 0x04  4 */ "Put",
	/* 0x05  5 */ "Delete Resp",
	/* 0x06  6 */ "Delete",
	/* 0x07  7 */ "Get Next Resp",
	/* 0x08  8 */ "Get Next",
	/* 0x09  9 */ "Get Previous Resp",
	/* 0x0a 10 */ "Get Previous",
	/* 0x0b 11 */ "Get Key Range Resp",
	/* 0x0c 12 */ "Get Key Range",
	/* 0x0d 13 */ "Reserved Message Type 13",
	/* 0x0e 14 */ "Reserved Message Type 14", 
	/* 0x0f 15 */ "Get Version Resp",
	/* 0x10 16 */ "Get Version",
	/* 0x11 17 */ "Reserved Message Type 17",
	/* 0x12 18 */ "Reserved Message Type 18",
	/* 0x13 19 */ "Reserved Message Type 19",
	/* 0x14 20 */ "Reserved Message Type 20",
	/* 0x15 21 */ "Setup Resp",
	/* 0x16 22 */ "Setup",
	/* 0x17 23 */ "Getlog Resp",
	/* 0x18 24 */ "Getlog",
	/* 0x19 25 */ "Security Resp",
	/* 0x1a 26 */ "Security",
	/* 0x1b 27 */ "Peer2Peer Push Resp",
	/* 0x1c 28 */ "Peer2Peer Push",
	/* 0x1d 29 */ "Noop Resp",
	/* 0x1e 30 */ "Noop",
	/* 0x1f 31 */ "Flush All Data Resp",
	/* 0x20 32 */ "Flush All Data",
	/* 0x21 33 */ "Reserved Message Type 33",
	/* 0x22 34 */ "Reserved Message Type 34",
	/* 0x23 35 */ "PIN Op Resp",
	/* 0x24 36 */ "PIN Op",
	/* 0x25 37 */ "Media Scan Resp",
	/* 0x26 38 */ "Media Scan",
	/* 0x27 39 */ "Mediaoptimize Resp",
	/* 0x28 40 */ "Mediaoptimize",
	/* 0x29 41 */ "Start Batch Resp",
	/* 0x2a 42 */ "Start Batch",
	/* 0x2b 43 */ "End Batch Resp",
	/* 0x2c 44 */ "End Batch",
	/* 0x2d 45 */ "Abort Batch Resp",
	/* 0x2e 46 */ "Abort Batch",
	/* 0x2f 47 */ "Set Power Level Resp",
	/* 0x30 48 */ "Set Power Level",
};
const int ki_msgtype_max = 48;

const char *ki_status_label1[] = {
	/* 0x00  0 */ "Not attempted",
	/* 0x01  1 */ "Success",
	/* 0x02  2 */ "HMAC failure",
	/* 0x03  3 */ "Not authorized",
	/* 0x04  4 */ "Version failure",
	/* 0x05  5 */ "Internal error",
	/* 0x06  6 */ "Header required",
	/* 0x07  7 */ "Not found",
	/* 0x08  8 */ "Version mismatch",
	/* 0x09  9 */ "Service busy",
	/* 0x0a 10 */ "Expired",
	/* 0x0b 11 */ "Data error",
	/* 0x0c 12 */ "Perm data error",
	/* 0x0d 13 */ "Remote connection error",
	/* 0x0e 14 */ "No space",
	/* 0x0f 15 */ "No such hmac algorithm",
	/* 0x10 16 */ "Invalid request",
	/* 0x11 17 */ "Nested operation errors",
	/* 0x12 18 */ "Device locked",
	/* 0x13 19 */ "Device already unlocked",
	/* 0x14 20 */ "Connection terminated",
	/* 0x15 21 */ "Invalid batch",
	/* 0x16 22 */ "Hibernate",
	/* 0x17 23 */ "Shutdown",
};
const int ki_status_max = 23;

const char *ki_cpolicy_label[] = {
	/* 0x00  0 */ "No Policy Defined",
	/* 0x01  1 */ "Write Through",
	/* 0x02  2 */ "Write Back",
	/* 0x03  3 */ "Flush",
};
const int ki_cpolicy_max = 3;
