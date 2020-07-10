#ifndef _GETLOG_H
#define _GETLOG_H
/**
 * Data structures specifically defined needed for the GETLOG request
 * See "Message GetLog" in kinetic.proto
 */
#define CGLT(cglt) COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__##cglt
typedef enum kgltype {
	KGLT_INVALID		= CGLT(INVALID_TYPE),
	KGLT_UTILIZATIONS	= CGLT(UTILIZATIONS),
	KGLT_TEMPERATURES	= CGLT(TEMPERATURES),
	KGLT_CAPACITIES		= CGLT(CAPACITIES),
	KGLT_CONFIGURATION	= CGLT(CONFIGURATION;),
	KGLT_STATISTICS		= CGLT(STATISTICS),
	KGLT_MESSAGES		= CGLT(MESSAGES),
	KGLT_LIMITS		= CGLT(LIMITS),
	KGLT_LOG		= CGLT(DEVICE),
} kgltype_t;


typedef struct kutilization {
	char *ku_name;		/* Name of the resource, null terminated */
	float ku_value;		/* Percent the resource is utilized  */
} kutilization_t;

typedef struct ktemprature {
	char * kt_name;		/* Descriptive name, "HDA" "Processor" */
	float ku_cur;		/* Current temp in C */
	float ku_min;		/* Minimum temp in C */
	float ku_max;		/* Maximum temp in C */
	float ku_target;	/* Target temp in C */
} ktemprature_t;

typedef struct kcapacity {
	uint64_t kc_total;	/* Total available bytes */
	float kc_used;		/* Percent of Total used */
} kcapacity_t;

typedef struct kinterface {
	char *ki_name;		/* Interface Name, e.g "eth0" */ 
	char *ki_mac;		/* MAC addr  e.g. "00:15:5d:e2:ef:17" */ 
	char *ki_ipv4;		/* IPv4 addr e.g. "192.168.200.33" */
	char *ki_ipv6;		/* IPv6 addr e.g. "fe80::215:5dff:fee2:ef17" */
} kinterface_t;

typedef struct kconfiguration {
	char *kcf_vendor;		/* Device Vendor Name  */
	char *kcf_model;		/* Device Model Number */
	char *kcf_serial;		/* Device Serial Number */
	char *kcf_wwn;			/* Device World Wide Name */
	char *kcf_version;		/* Device Version */
	char *kcf_compdate;		/* Device Firmware Compilation Date */
	char *kcf_srchash;		/* Firmware Source Code Hash */
	char *kcf_proto;		/* Kinetic Protocol Version Number */
	char *kcf_protocompdate;	/* Kinetic Protocol Compilation Date */
	char *kcf_protosrchash;		/* Kinetic Protocol Source Code Hash */
	kinterface_t *kcf_interfaces;	/* Device Interface List */
	uint32_t kcf_port;		/* Device Unencrypted Port */
	uint32_t kcf_tlsport;		/* Device Encrypted Port */
	kpowerlevel_t kcf_power;	/* Device Current Power Level */
} kconfiguration_t;

typedef struct kstatistic {
	kmtype_t ks_mtype;
	uint64_t ks_cnt;
	uint64_t ks_bytes;
} kstatistic_t;

/** 
 * klimits structure. 
 * These are all maximums. 
 * structure elements that are "len"s are in bytes.
 * structure elements that are "cnt"s are in individual units
 */ 
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
typedef struct kdevicelog {
	char *kdl_name;		/* Log name to be fetched, null terminated */
} kdevice_t;

 typedef struct kgetlog {
	 kgltype_t		*kgl_type;
	 uint32_t		kgl_typecnt;
	 
	 kutilization_t		*kgl_util;
	 uint32_t		kgl_utilcnt;

	 ktemperature_t		*kgl_temp;
	 uint32_t		kgl_tempcnt;
	 
	 kcapacitiy_t		kgl_cap;
	 kconfiguration_t	kgl_conf;
	 
	 kstatistic_t		*kgl_stat;
	 uint32_t		kgl_statcnt;

	 char 			*kgl_msgs;
	 uint32_t		kgl_msgscnt;

	 klimits_t		kgl_limits;
	 kdevicelog_t		kgl_log;
 } kgetlog_t;


#endif /* _GETLOG_H */

