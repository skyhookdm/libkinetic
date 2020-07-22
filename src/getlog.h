/**
 * Data structures specifically defined needed for the GETLOG request
 * See "Message GetLog" in kinetic.proto
 */
#ifndef _GETLOG_H
#define _GETLOG_H

#include "kinetic.h"

/**
 * Forward declarations
 */
struct kbuffer;
struct kgetlog;

typedef struct kgetlog kgetlog_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog							kproto_getlog_t;
typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Device					kgetlog_device_info;


/* ------------------------------
 * Data types for outgoing GetLog requests
 */

// Convenience macros for long token names
#define CGLT(cglt) COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__##cglt

// Information types (categories) for GetLog requests (defined as `Type` in protobuf definition).
// There is a typedef for the generated enum structure, and an unnamed enum to alias values of the
// generated enum structure.
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


/* ------------------------------
 * Data types for incoming GetLog responses
 */

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Utilization kproto_utilization_t;
typedef struct kutilization {
	char  *ku_name;
	float  ku_value;
} kutilization_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Temperature kproto_temperature_t;
typedef struct ktemperature {
	char  *kt_name;    /* Descriptive name, "HDA" "Processor" */
	float  kt_cur;	   /* Current temp in C */
	float  kt_min;	   /* Minimum temp in C */
	float  kt_max;	   /* Maximum temp in C */
	float  kt_target;  /* Target temp in C */
} ktemperature_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Capacity kproto_capacity_t;
typedef struct kcapacity {
	uint64_t kc_total; /* Total available bytes */
	float	 kc_used;  /* Percent of Total used */
} kcapacity_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration__Interface kproto_interface_t;
typedef struct kinterface {
	char *ki_name;		/* Interface Name, e.g "eth0" */
	char *ki_mac;		/* MAC addr  e.g. "00:15:5d:e2:ef:17" */
	char *ki_ipv4;		/* IPv4 addr e.g. "192.168.200.33" */
	char *ki_ipv6;		/* IPv6 addr e.g. "fe80::215:5dff:fee2:ef17" */
} kinterface_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Configuration kproto_configuration_t;
typedef struct kconfiguration {
	char		*kcf_vendor;	  /* Device Vendor Name  */
	char		*kcf_model;	  /* Device Model Number */
	char		*kcf_serial;	  /* Device Serial Number */
	char		*kcf_wwn;	  /* Device World Wide Name */
	char		*kcf_version;	  /* Device Version */
	char		*kcf_compdate;	  /* Device Firmware Compilation Date */
	char		*kcf_srchash;	  /* Firmware Source Code Hash */
	char		*kcf_proto;	  /* Kinetic Protocol Version Number */
	char		*kcf_protocompdate; /* Kinetic Proto Compilation Date */
	char		*kcf_protosrchash;  /* Kinetic Proto Source Code Hash */
	kinterface_t	*kcf_interfaces;  /* Device Interface List */
	uint32_t	kcf_port;	  /* Device Unencrypted Port */
	uint32_t	kcf_tlsport;	  /* Device Encrypted Port */

	/* Device Current Power Level */
	Com__Seagate__Kinetic__Proto__Command__PowerLevel kcf_power;
} kconfiguration_t;

typedef Com__Seagate__Kinetic__Proto__Command__GetLog__Statistics kproto_statistics_t;
typedef struct kstatistics {
	Com__Seagate__Kinetic__Proto__Command__MessageType ks_mtype;

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
	uint32_t kl_keylen;		/* Maximum Key Length supported */
	uint32_t kl_vallen;		/* Maximum Value Length supported */
	uint32_t kl_verlen;		/* Maximum Version Length supported */
	uint32_t kl_taglen;		/* Maximum Tag Length supported */
	uint32_t kl_msglen;		/* Maximum Message Length supported */
	uint32_t kl_pinlen;		/* Maximum Pin Length supported */
	uint32_t kl_batlen;		/* Maximum Batch Length supported */
	uint32_t kl_pendrdcnt;		/* Total Allowed Pending RD Reqs */
	uint32_t kl_pendwrcnt;		/* Total Allowed Pending WR Reqs */
	uint32_t kl_conncnt;		/* Total Allowed Client Connections */
	uint32_t kl_idcnt;			/* Total Allowed User IDs */
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
	size_t	kdl_len;
} kdevicelog_t;


struct kgetlog {
	kgltype_t		*kgl_type;
	size_t			kgl_typecnt;

	kutilization_t		*kgl_util;
	uint32_t		kgl_utilcnt;

	ktemperature_t		*kgl_temp;
	uint32_t		kgl_tempcnt;

	kcapacity_t		kgl_cap;
	kconfiguration_t  	kgl_conf;

	kstatistics_t		*kgl_stat;
	uint32_t		kgl_statcnt;

	char			*kgl_msgs;
	size_t			kgl_msgslen;

	klimits_t		kgl_limits;
	kdevicelog_t		kgl_log;
};

#endif /* _GETLOG_H */
