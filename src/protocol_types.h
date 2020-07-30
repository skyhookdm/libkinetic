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
 * Forward declarations
 */

struct kiovec;
struct kv;

// defined in kinetic.h
struct kv;


/* ------------------------------
 * Aliases for Message types
 */
typedef Com__Seagate__Kinetic__Proto__Command		    kproto_cmd_t;
typedef Com__Seagate__Kinetic__Proto__Command__Header   kproto_cmdhdr_t;
typedef Com__Seagate__Kinetic__Proto__Command__Body     kproto_body_t;
typedef Com__Seagate__Kinetic__Proto__Command__Status   kproto_status_t;

typedef Com__Seagate__Kinetic__Proto__Command__KeyValue kproto_kv_t;
typedef Com__Seagate__Kinetic__Proto__Command__Range    kproto_keyrange_t;


/* ------------------------------
 * Aliases for Field types
 */

/* Aliases for structs */

// Field types for requests
typedef Com__Seagate__Kinetic__Proto__Command__Priority kproto_priority;

/* Aliases for enums */

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



/* creating and extracting to/from protobuf structures */
struct kresult_message  create_header(uint8_t header_field_bitmap, ...);
ProtobufCBinaryData     create_command_bytes(kproto_cmdhdr_t *cmd_hdr, void *proto_cmd);

int keyname_to_proto(ProtobufCBinaryData *proto_keyval, struct kiovec *keynames, size_t keycnt);

/* serialization/deserialization */
kproto_cmd_t        *unpack_kinetic_command(ProtobufCBinaryData commandbytes);
ProtobufCBinaryData  pack_kinetic_command(kproto_cmd_t *cmd_data);

/* ktli helpers for directly accessing fields */
uint64_t ki_getaseq(struct kiovec *msg, int msgcnt);
void     ki_setseq(struct kiovec *msg, int msgcnt, uint64_t seq);

/* resource management */
void destroy_command(void *unpacked_cmd);

#endif //__KINETIC_TYPES_H
