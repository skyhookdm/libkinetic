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

#ifndef __PROTOCOL_TYPES_H
#define __PROTOCOL_TYPES_H


#include "protocol/kinetic.pb-c.h"

/**
 * This header defines structs for the kinetic library, and functions for the structs defined.
 *
 * Typically, once would want to implement these and provide mappings to the generated wire
 * protocol code. This library actually provides a thin wrapper around the generated protobuf code,
 * as these structures should serve perfectly well for passing data around.
 */

// Macros
#define HEADER_FIELD_COUNT 7


// ------------------------------
// Aliases for protobuf structs (that do not have a front-end data type; further below)

// Communication with kinetic server uses `Message` type.
// A `Message` contains packed `Command` data
typedef Com__Seagate__Kinetic__Proto__Message                 kproto_msg_t;
typedef Com__Seagate__Kinetic__Proto__Message__HMACauth       kproto_hmacauth_t;
typedef Com__Seagate__Kinetic__Proto__Message__PINauth        kproto_pinauth_t;

// Input data for kinetic operations are contained in `Command` types
typedef Com__Seagate__Kinetic__Proto__Command                 kproto_cmd_t;
typedef Com__Seagate__Kinetic__Proto__Command__Body           kproto_body_t;
typedef Com__Seagate__Kinetic__Proto__Command__Status         kproto_status_t;

typedef Com__Seagate__Kinetic__Proto__Command__KeyValue       kproto_kv_t;
typedef Com__Seagate__Kinetic__Proto__Command__Range          kproto_keyrange_t;
typedef Com__Seagate__Kinetic__Proto__Command__Batch          kproto_batch_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog         kproto_getlog_t;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Device kgetlog_device_info;
typedef Com__Seagate__Kinetic__Proto__Command__ManageApplet   kproto_kapplet_t;

// ------------------------------
// Aliases for protobuf enums

// Kinetic Message Types, ie Kinetic Ops
#define CMT(cmt) COM__SEAGATE__KINETIC__PROTO__COMMAND__MESSAGE_TYPE__##cmt

typedef Com__Seagate__Kinetic__Proto__Command__MessageType kmtype_t;
enum {
	KMT_INVALID   = CMT(INVALID_MESSAGE_TYPE),
	KMT_GET       = CMT(GET),
	KMT_PUT       = CMT(PUT),
	KMT_DEL       = CMT(DELETE),
	KMT_GETNEXT   = CMT(GETNEXT),
	KMT_GETPREV   = CMT(GETPREVIOUS),
	KMT_GETRANGE  = CMT(GETKEYRANGE),
	KMT_GETVERS   = CMT(GETVERSION),
	KMT_SETUP     = CMT(SETUP),
	KMT_GETLOG    = CMT(GETLOG),
	KMT_SECURITY  = CMT(SECURITY),
	KMT_PUSHP2P   = CMT(PEER2PEERPUSH),
	KMT_NOOP      = CMT(NOOP),
	KMT_FLUSH     = CMT(FLUSHALLDATA),
	KMT_PINOP     = CMT(PINOP),
	KMT_SCANMEDIA = CMT(MEDIASCAN),
	KMT_OPTMEDIA  = CMT(MEDIAOPTIMIZE),
	KMT_STARTBAT  = CMT(START_BATCH),
	KMT_ENDBAT    = CMT(END_BATCH),
	KMT_ABORTBAT  = CMT(ABORT_BATCH),
	KMT_SETPOWER  = CMT(SET_POWER_LEVEL),
	KMT_APPLET    = CMT(MANAGE_APPLET),
};


// Authorization types (`AuthType`)
#define KMAT(ka) COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__##ka

typedef Com__Seagate__Kinetic__Proto__Message__AuthType kauth_t;
enum {
	KAT_INVALID     = KMAT(INVALID_AUTH_TYPE),
	KAT_HMAC        = KMAT(HMACAUTH)         ,
	KAT_PIN         = KMAT(PINAUTH)          ,
	KAT_UNSOLICITED = KMAT(UNSOLICITEDSTATUS),
};

// Prioritization of commands
#define KPCP(kp) COM__SEAGATE__KINETIC__PROTO__COMMAND__PRIORITY__##kp

typedef Com__Seagate__Kinetic__Proto__Command__Priority kpriority_t;
enum {
	LOWEST  = KPCP(LOWEST)      ,
	LOWER   = KPCP(LOWER)       ,
	LOW     = KPCP(LOW)         ,
	LNORMAL = KPCP(LOWERNORMAL) ,
	NORMAL  = KPCP(NORMAL)      ,
	HNORMAL = KPCP(HIGHERNORMAL),
	HIGH    = KPCP(HIGH)        ,
	HIGHER  = KPCP(HIGHER)      ,
	HIGHEST = KPCP(HIGHEST)     ,
};


// Kinetic Server Power Levels
#define CPLT(cplt) COM__SEAGATE__KINETIC__PROTO__COMMAND__POWER_LEVEL__##cplt

typedef Com__Seagate__Kinetic__Proto__Command__PowerLevel kpltype_t;
enum {
	KPLT_INVALID     = CPLT(INVALID_LEVEL),
	KPLT_OPERATIONAL = CPLT(OPERATIONAL)  ,
	KPLT_HIBERNATE   = CPLT(HIBERNATE)    ,
	KPLT_SHUTDOWN    = CPLT(SHUTDOWN)     ,
	KPLT_FAIL        = CPLT(FAIL)         ,
};


// Information types (categories) for GetLog requests
// (defined as `Type` in protobuf definition).
#define CGLT(cglt) COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__##cglt

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Type kgltype_t;
enum {
	KGLT_INVALID       = CGLT(INVALID_TYPE) ,
	KGLT_UTILIZATIONS  = CGLT(UTILIZATIONS) ,
	KGLT_TEMPERATURES  = CGLT(TEMPERATURES) ,
	KGLT_CAPACITIES    = CGLT(CAPACITIES)   ,
	KGLT_CONFIGURATION = CGLT(CONFIGURATION),
	KGLT_STATISTICS    = CGLT(STATISTICS)   ,
	KGLT_MESSAGES      = CGLT(MESSAGES)     ,
	KGLT_LIMITS        = CGLT(LIMITS)       ,
	KGLT_LOG           = CGLT(DEVICE)       ,
};


// ------------------------------
// Aliases and front-end structs (paired, respectively) for protobuf payload data

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Utilization kproto_utilization_t;
typedef struct kutilization {
	char  *ku_name;
	float  ku_value;
} kutilization_t;


typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Temperature kproto_temperature_t;
typedef struct ktemperature {
	char  *kt_name;    // Descriptive name, "HDA" "Processor"
	float  kt_cur;     // Current temp in C
	float  kt_min;     // Minimum temp in C
	float  kt_max;     // Maximum temp in C
	float  kt_target;  // Target temp in C
} ktemperature_t;


typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Capacity kproto_capacity_t;
typedef struct kcapacity {
	uint64_t kc_total; // Total available bytes
	float    kc_used;  // Percent of Total used
} kcapacity_t;


typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration__Interface kproto_interface_t;
typedef struct kinterface {
	char *ki_name;		// Interface Name, e.g "eth0"
	char *ki_mac;		// MAC addr  e.g. "00:15:5d:e2:ef:17"
	char *ki_ipv4;		// IPv4 addr e.g. "192.168.200.33"
	char *ki_ipv6;		// IPv6 addr e.g. "fe80::215:5dff:fee2:ef17"
} kinterface_t;


typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration kproto_configuration_t;
typedef struct kconfiguration {
	char         *kcf_vendor;        // Device Vendor Name
	char         *kcf_model;         // Device Model Number
	char         *kcf_serial;        // Device Serial Number
	char         *kcf_wwn;           // Device World Wide Name
	char         *kcf_version;       // Device Version
	char         *kcf_compdate;      // Device Firmware Compilation Date
	char         *kcf_srchash;       // Firmware Source Code Hash
	char         *kcf_proto;         // Kinetic Protocol Version Number
	char         *kcf_protocompdate; // Kinetic Proto Compilation Date
	char         *kcf_protosrchash;  // Kinetic Proto Source Code Hash
	kinterface_t *kcf_interfaces;    // Device Interface List
	uint32_t      kcf_interfacescnt; // Device Interface List Count
	uint32_t      kcf_port;          // Device Unencrypted Port
	uint32_t      kcf_tlsport;       // Device Encrypted Port
	kpltype_t     kcf_power;         // Device Current Power Level
} kconfiguration_t;


typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Statistics kproto_statistics_t;
typedef struct kstatistics {
	kmtype_t ks_mtype;
	uint64_t ks_cnt;
	uint64_t ks_bytes;
	uint64_t ks_maxlatency;
} kstatistics_t;


/**
 * Device limits as maximum values:
 *	- elements with "len" suffix are in bytes.
 *	- elements with "cnt" suffix are in units.
 */
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Limits kproto_limits_t;
typedef struct klimits {
	/* lens in bytes */
	uint32_t kl_keylen;		/* Maximum Key Length supported */
	uint32_t kl_vallen;		/* Maximum Value Length supported */
	uint32_t kl_verlen;		/* Maximum Version Length supported */
	uint32_t kl_disumlen;		/* Maximum Chksum Length supported */
	uint32_t kl_msglen;		/* Maximum Message Length supported */
	uint32_t kl_pinlen;		/* Maximum Pin Length supported */
	uint32_t kl_batlen;		/* Maximum Batch Length supported */

	/* all cnts in unit counts */
	uint32_t kl_pendrdcnt;		/* Total Allowed Pending RD Reqs */
	uint32_t kl_pendwrcnt;		/* Total Allowed Pending WR Reqs */
	uint32_t kl_conncnt;		/* Total Allowed Client Connections */
	uint32_t kl_idcnt;		/* Total Allowed User IDs */
	uint32_t kl_rangekeycnt;	/* Total Allowed Keys/Range Op */
	uint32_t kl_batopscnt;		/* Total Allowed Ops/Batch */
	uint32_t kl_batdelcnt;		/* Total Allowed Delete Ops/Batch */
	uint32_t kl_devbatcnt;		/* Total Allowed Open Batches/Device */
} klimits_t;


/*
 * Generic device log. This is a logname that is provided by the server.
 * Currently there are no generic lags available. But could be anything
 * provided by the kinetic server. Usually these will be vendor extensions
 * to the getlog.  A vendor kinetic server is encouraged to name their logs
 * with a vendor specific prefix, i.e. "com.WD.glog".
 */
//NOTE: should this have a length? see getlog.c:31
typedef struct kdevicelog {
	char   *kdl_name;
	size_t  kdl_len;
} kdevicelog_t;


typedef struct kmsghdr {
	kauth_t   kmh_atype;   // Message Auth Type
	int64_t   kmh_id;      // if HMAC, HMAC ID
	void     *kmh_hmac;    // if HMAC, HMAC byte string
	uint32_t  kmh_hmaclen; // if HMAC, HMAC length
	void     *kmh_pin;     // if PIN, PIN byte string
	uint32_t  kmh_pinlen;  // if PIN, PIN Length
} kmsghdr_t;

typedef uint64_t kseq_t;
typedef uint32_t kbid_t;

typedef Com__Seagate__Kinetic__Proto__Command__Header kproto_cmdhdr_t;
typedef struct kcmdhdr {
	int64_t     kch_clustvers; // Cluster Version Number
	int64_t     kch_connid;    // Connection ID
	kseq_t      kch_seq;       // Request Sequence Number
	kseq_t      kch_ackseq;    // Response Sequence Number
	kmtype_t    kch_type;      // Request Message Type
	uint64_t    kch_timeout;   // Timeout Period
	kpriority_t kch_pri;       // Request Priority
	uint64_t    kch_quanta;    // Time Quanta
	int32_t     kch_qexit;     // Boolean: Quick Exit
	kbid_t      kch_bid;       // Batch ID
} kcmdhdr_t;


/**
 * Kinetic PDU structure
 * The Kinetic pdu are the first bits on the wire and defines the total
 * length of the elements that follow.  The first element is a magic byte
 * that defines the beginning of the PDU - it is set to 'F' or 0x46, then
 * the length of the kinetic protobuf message and then finally the length
 * of the value, if any.
 *
 *    offset   Description                                    length
 *   +------+------------------------------------------------+------+
 *   |  0   |  Kinetic Magic   - Must be 'F' 0x46            |  1   |
 *   +------+------------------------------------------------+------+
 *   |  1   |  Protobuf Length - BE, Bounded 0 <  L <= 1024k |  4   |
 *   +------+------------------------------------------------+------+
 *   |  5   |  Value Length    - BE, Bounded 0 <= L <= 1024k |  4   |
 *   +------+------------------------------------------------+------+
 *
 * This is the layout on the wire and consequently a C data structure
 * cannot be overlayed due to C's padding of structures. So PACK and UNPACK
 * macros are provided to move the header in and out of the kpdu_t structure
 * below. The UNPACK macros pull the bytes out of the packed buffer and
 * then apply the local ntohl() macros to convert to the local endianness.
 * The same two step process is used for the PACK macros as well. The two
 * step process is for portability between big and little endian architectures
 */
typedef struct kpdu {
	uint8_t		kp_magic;	// 0x00, Always 'F' 0x46
	uint32_t	kp_msglen;	// 0x04, Length of protobuf message
	uint32_t	kp_vallen;	// 0x08, Length of the value
} kpdu_t;


#endif // __PROTOCOL_TYPES_H
