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


#include <stdarg.h>

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
/*

typedef Com__Seagate__Kinetic__Proto__Command__Status	kproto_status;
*/
typedef Com__Seagate__Kinetic__Proto__Message		  kmsg_t;
typedef Com__Seagate__Kinetic__Proto__Command		  kcmd_t;
typedef Com__Seagate__Kinetic__Proto__Command__Header kcmd_hdr_t;
typedef Com__Seagate__Kinetic__Proto__Command__Body   kcmd_body_t;


/* ------------------------------
 * Aliases for Field types
 */

/* Aliases for structs */

// Field types for requests
typedef Com__Seagate__Kinetic__Proto__Command__Priority kproto_priority;

/* Aliases for enums */

/**
 * Kinetic Status Codes
 */

#define CSSC(cssc) COM__SEAGATE__KINETIC__PROTO__COMMAND__STATUS__STATUS_CODE__##cssc

typedef Com__Seagate__Kinetic__Proto__Command__Status__StatusCode kstatus_code_t;
enum {
	K_INVALID_SC   = CSSC(INVALID_STATUS_CODE),
	K_OK		   = CSSC(SUCCESS),
	K_EREJECTED    = CSSC(NOT_ATTEMPTED),
	K_EHMAC		   = CSSC(HMAC_FAILURE),
	K_EACCESS	   = CSSC(NOT_AUTHORIZED),
	K_EVERSION	   = CSSC(VERSION_FAILURE),
	K_EINTERNAL    = CSSC(INTERNAL_ERROR),
	K_ENOHEADER    = CSSC(HEADER_REQUIRED),
	K_ENOTFOUND    = CSSC(NOT_FOUND),
	K_EBADVERS	   = CSSC(VERSION_MISMATCH),
	K_EBUSY		   = CSSC(SERVICE_BUSY),
	K_ETIMEDOUT    = CSSC(EXPIRED),
	K_EDATA		   = CSSC(DATA_ERROR),
	K_EPERMDATA    = CSSC(PERM_DATA_ERROR),
	K_EP2PCONN	   = CSSC(REMOTE_CONNECTION_ERROR),
	K_ENOSPACE	   = CSSC(NO_SPACE),
	K_ENOHMAC	   = CSSC(NO_SUCH_HMAC_ALGORITHM),
	K_EINVAL	   = CSSC(INVALID_REQUEST),
	K_EP2P		   = CSSC(NESTED_OPERATION_ERRORS),
	K_ELOCKED	   = CSSC(DEVICE_LOCKED),
	K_ENOTLOCKED   = CSSC(DEVICE_ALREADY_UNLOCKED),
	K_ECONNABORTED = CSSC(CONNECTION_TERMINATED),
	K_EINVALBAT    = CSSC(INVALID_BATCH),
	K_EHIBERNATE   = CSSC(HIBERNATE),
	K_ESHUTDOWN    = CSSC(SHUTDOWN),
};

/**
 * Kinetic Data Integrity supported algrithms.
 */
#define CA(ca) COM__SEAGATE__KINETIC__PROTO__COMMAND__ALGORITHM__##ca

typedef Com__Seagate__Kinetic__Proto__Command__Algorithm kditype_t;
enum {
	KDI_INVALID = CA(INVALID_ALGORITHM),
	KDI_SHA1	= CA(SHA1)			   ,
	KDI_SHA2	= CA(SHA2)			   ,
	KDI_SHA3	= CA(SHA3)			   ,
	KDI_CRC32C	= CA(CRC32C)		   ,
	KDI_CRC64	= CA(CRC64)			   ,
	KDI_CRC32	= CA(CRC32)			   ,
};

/**
 * Kinetic Message Types, ie Kinetic Ops
 */
#define CMT(cmt) COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__##cmt

typedef Com__Seagate__Kinetic__Proto__Command__MessageType kmtype_t;
enum {
	KMT_INVALID   = CMT(INVALID_MESSAGE_TYPE),
	KMT_GET		  = CMT(GET),
	KMT_PUT		  = CMT(PUT),
	KMT_DEL		  = CMT(DELETE),
	KMT_GETNEXT   = CMT(GETNEXT),
	KMT_GETPREV   = CMT(GETPREVIOUS),
	KMT_GETRANGE  = CMT(GETKEYRANGE),
	KMT_GETVERS   = CMT(GETVERSION),
	KMT_SETUP	  = CMT(SETUP),
	KMT_GETLOG	  = CMT(GETLOG),
	KMT_SECURITY  = CMT(SECURITY),
	KMT_PUSHP2P   = CMT(PEER2PEERPUSH),
	KMT_NOOP	  = CMT(NOOP),
	KMT_FLUSH	  = CMT(FLUSHALLDATA),
	KMT_PINOP	  = CMT(PINOP),
	KMT_SCANMEDIA = CMT(MEDIASCAN),
	KMT_OPTMEDIA  = CMT(MEDIAOPTIMIZE),
	KMT_STARTBAT  = CMT(START_BATCH),
	KMT_ENDBAT	  = CMT(END_BATCH),
	KMT_ABORTBAT  = CMT(ABORT_BATCH),
	KMT_SETPOWER  = CMT(SET_POWER_LEVEL),
};

/**
 * Kinetic Cache Policies
 */
#define CS(cs) COM__SEAGATE__KINETIC__PROTO__COMMAND__SYNCHRONIZATION__##cs

typedef Com__Seagate__Kinetic__Proto__Command__Synchronization kcachepolicy_t;
enum {
	KC_INVALID = CS(INVALID_SYNCHRONIZATION),
	KC_WT      = CS(WRITETHROUGH)           ,
	KC_WB      = CS(WRITEBACK)              ,
	KC_FLUSH   = CS(FLUSH)                  ,
};

/**
 * Kinetic Server Power Levels,
 * PAK: probably should be in management.h
 */
#define CPLT(cplt) COM__SEAGATE__KINETIC__PROTO__COMMAND__POWER_LEVEL__##cplt

typedef Com__Seagate__Kinetic__Proto__Command__PowerLevel kpltype_t;
enum {
	KPLT_INVALID     = CPLT(INVALID_LEVEL),
	KPLT_OPERATIONAL = CPLT(OPERATIONAL)  ,
	KPLT_HIBERNATE   = CPLT(HIBERNATE)    ,
	KPLT_SHUTDOWN    = CPLT(SHUTDOWN)     ,
	KPLT_FAIL        = CPLT(FAIL)         ,
};


/* ------------------------------
 * Custom types
 */

// For Messages
typedef struct kmsg_auth {
	kauth_t  auth_type;
	int64_t  hmac_identity;  /* Only use if auth_type is HMAC */
	size_t   auth_len;
	char    *auth_data;		 /* PIN or HMAC data */
} kmsg_auth_t ;


// Types for interfacing with protobufs
enum header_field_type {
	CLUST_VER	 = (uint8_t) 1 << 0,
	CONN_ID		 = (uint8_t) 1 << 1,
	SEQ_NUM		 = (uint8_t) 1 << 2,
	TIMEOUT		 = (uint8_t) 1 << 3,
	PRIORITY	 = (uint8_t) 1 << 4,
	TIME_QUANTA  = (uint8_t) 1 << 5,
	BATCH_ID	 = (uint8_t) 1 << 6,
};


/* ------------------------------
 * General protocol API
 */

struct kresult_message create_header(uint8_t header_field_bitmap, ...);
struct kresult_message create_message(kmsg_auth_t *msg_auth, ProtobufCBinaryData cmd_bytes);

struct kresult_buffer  pack_kinetic_message(kmsg_t *msg_data);
ProtobufCBinaryData    pack_kinetic_command(kcmd_t *cmd_data);


#endif //__KINETIC_TYPES_H
