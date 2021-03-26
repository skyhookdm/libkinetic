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
#ifndef _KINETIC_TYPES_H
#define _KINETIC_TYPES_H

#include "protocol/kinetic.pb-c.h"
#include "protocol_types.h"


/* 
 * Below are the types required to use the create/clean/destroy interface
 */
typedef enum ktype {
	KT_NONE	= 0,
	KV_T,
	KRANGE_T,
	KITER_T,
	KBATCH_T,
	KGETLOG_T,
	KVERSION_T,
	KSTATS_T,
	/* Keep last */
	KT_LAST,	
} ktype_t;

/* Initial batch id for each Session */
#define KFIRSTBID 1000

// ------------------------------
// Type aliases for protocol

// Kinetic Data Integrity supported algrithms.
#define CA(ca) COM__SEAGATE__KINETIC__PROTO__COMMAND__ALGORITHM__##ca

typedef Com__Seagate__Kinetic__Proto__Command__Algorithm kditype_t;
enum {
	KDI_INVALID = CA(INVALID_ALGORITHM),
	KDI_SHA1    = CA(SHA1)             ,
	KDI_SHA2    = CA(SHA2)             ,
	KDI_SHA3    = CA(SHA3)             ,
	KDI_CRC32C  = CA(CRC32C)           ,
	KDI_CRC64   = CA(CRC64)            ,
	KDI_CRC32   = CA(CRC32)            ,
};

// Kinetic Cache Policies
#define CS(cs) COM__SEAGATE__KINETIC__PROTO__COMMAND__SYNCHRONIZATION__##cs

typedef Com__Seagate__Kinetic__Proto__Command__Synchronization kcachepolicy_t;
enum {
	KC_INVALID = CS(INVALID_SYNCHRONIZATION),
	KC_WT      = CS(WRITETHROUGH)           ,
	KC_WB      = CS(WRITEBACK)              ,
	KC_FLUSH   = CS(FLUSH)                  ,
};


// Kinetic Status Codes
#define CSSC(cssc) COM__SEAGATE__KINETIC__PROTO__COMMAND__STATUS__STATUS_CODE__##cssc

//typedef Com__Seagate__Kinetic__Proto__Command__Status__StatusCode kstatus_code_t;
typedef enum kstatus {
	/* Error #'s fixed by kinetic.proto */
	K_INVALID_SC	= CSSC(INVALID_STATUS_CODE),	/* -1 */

	KSTAT_GRP1	= 0,
	K_EREJECTED	= CSSC(NOT_ATTEMPTED),		/*  0 */
	K_OK		= CSSC(SUCCESS),		/*  1 */
	K_EHMAC		= CSSC(HMAC_FAILURE),		/*  2 */
	K_EACCESS	= CSSC(NOT_AUTHORIZED),		/*  3 */
	K_EVERSION	= CSSC(VERSION_FAILURE),	/*  4 */
	K_EINTERNAL	= CSSC(INTERNAL_ERROR),		/*  5 */
	K_ENOHEADER	= CSSC(HEADER_REQUIRED),	/*  6 */
	K_ENOTFOUND	= CSSC(NOT_FOUND),		/*  7 */
	K_EBADVERS	= CSSC(VERSION_MISMATCH),	/*  8 */
	K_EBUSY		= CSSC(SERVICE_BUSY),		/*  9 */
	K_ETIMEDOUT	= CSSC(EXPIRED),		/* 10 */
	K_EDATA		= CSSC(DATA_ERROR),		/* 11 */
	K_EPERMDATA	= CSSC(PERM_DATA_ERROR),	/* 12 */
	K_EP2PCONN	= CSSC(REMOTE_CONNECTION_ERROR),/* 13 */
	K_ENOSPACE	= CSSC(NO_SPACE),		/* 14 */
	K_ENOHMAC	= CSSC(NO_SUCH_HMAC_ALGORITHM),	/* 15 */
	K_EINVAL	= CSSC(INVALID_REQUEST),	/* 16 */
	K_EP2P		= CSSC(NESTED_OPERATION_ERRORS),/* 17 */
	K_ELOCKED	= CSSC(DEVICE_LOCKED),		/* 18 */
	K_ENOTLOCKED	= CSSC(DEVICE_ALREADY_UNLOCKED),/* 19 */
	K_ECONNABORTED	= CSSC(CONNECTION_TERMINATED),	/* 20 */
	K_EINVALBAT	= CSSC(INVALID_BATCH),		/* 21 */
	K_EHIBERNATE	= CSSC(HIBERNATE),		/* 22 */
	K_ESHUTDOWN	= CSSC(SHUTDOWN),  		/* 23 */
	KSTAT_GRP1_LAST	= 24,

	/* Need more errnos, Sufficiently outside kinetic.proto */
	KSTAT_GRP2	= 0x8000, 		/* Prefix for nonproto errors */
	K_EAGAIN	= (KSTAT_GRP2 | 0),
	K_ENOMSG	= (KSTAT_GRP2 | 1),
	K_ENOMEM	= (KSTAT_GRP2 | 2),
	K_EBADSESS	= (KSTAT_GRP2 | 3),
	K_EBATCH	= (KSTAT_GRP2 | 4),
	KSTAT_GRP2_LAST	= 0,
	
} kstatus_t;

/**
 * koivec structure
 *
 * Analogous to Linux's iovec structure, the kiovec struct is used to move
 * around keys and values that may be composed from many components.
 * This is used in many places in libkinetic, including KTLI.
 */
struct kiovec {
	size_t  kiov_len;  // Number of bytes
	void   *kiov_base; // Starting address
};


typedef struct kgetlog {
	kgltype_t        *kgl_type;
	size_t            kgl_typecnt;

	kutilization_t   *kgl_util;
	uint32_t          kgl_utilcnt;

	ktemperature_t   *kgl_temp;
	uint32_t          kgl_tempcnt;

	kcapacity_t       kgl_cap;
	kconfiguration_t  kgl_conf;

	kstatistics_t    *kgl_stat;
	uint32_t          kgl_statcnt;

	char             *kgl_msgs;
	size_t            kgl_msgslen;

	klimits_t         kgl_limits;
	kdevicelog_t      kgl_log;

	void           *kgl_protobuf;

	// NOTE: this also frees getlog_data, unless we don't want it to
	void           (*destroy_protobuf)(struct kgetlog *getlog_data);
} kgetlog_t;

/**
 * kv_t structure
 *
 * kv_key	Is a kiovec vector(array) of buffer(s) containing a single
 *		key. The vector permits keys to be easily prefixed or
 *		postfixed without the need of a copy into a single contiguous
 *		buffer.
 * kv_keycnt	The number of elements in the key vector
 * kv_val	Is a kiovec vector(array) of buffer(s) containing a single
 *		value. The vector permits keys to be easily prefixed or
 *		postfixed without the need of a copy into a single contiguous
 *		buffer.
 * kv_valcnt	The number of elements in the value vector
 * kv_ver	Is the current version of the key value pair,
 * 		most servers use a string numeric value, e.g. "000003"
 * kv_verlen	Length of the kv_ver byte string
 * kv_newver	Is the new version of the key value pair to be set,
 * 		most servers use a string numeric value, e.g. "000003"
 * kv_newverlen Length of the kv_newver byte string
 * kv_disum	Is the data integrity chksum. It is a computed sum stored as
 *  		byte string. kv_ditype must match the algorithm used to
 *		compute the di sum.
 * kv_disumlen	Length of the kv_disum byte string
 * kv_ditype	Specifies the alrorithm used to calculate the di chksum, for
 *		server mediascan you should use either: SHA1, SHA2, SHA3,
 *		CRC32, CRC32C or CRC64
 * kv_cpolicy	This specifies the caching policy for this key value. It must
 * 		be specified on each operation. Can be write through,
 * 		write back or flush.
 */
typedef struct kv {
	struct kiovec  *kv_key;
	size_t          kv_keycnt;
	struct kiovec  *kv_val;
	size_t          kv_valcnt;
	void           *kv_ver;
	size_t          kv_verlen;
	void           *kv_newver;
	size_t          kv_newverlen;
	void           *kv_disum;
	size_t          kv_disumlen;
	kditype_t       kv_ditype;
	kcachepolicy_t  kv_cpolicy;
	uint32_t	kv_metaonly;

	/* NOTE: currently, this also frees kv_data */
	void        *kv_protobuf;
	void        (*destroy_protobuf)(struct kv *kv_data);
} kv_t;

/**
 * Key Range structure
 *
 * Key ranges are key spaces, denoted by the 5-tuple,
 * 	<start, end, starti, endi, count>
 * 	o Start key. This key denotes the beginning of the range. This key
 * 	  and may or may not exist. The specification of just a start key is
 *	  interpretted to mean non-inclusive to that key, i.e. first key
 * 	  in the range will be the key that directly follows the start key.
 *	  A NULL start key is interpretted as an empty string i.e. the first
 *	  possible key.
 *	o End key. This key denotes the ending of the range. This key
 *	  and may or may not exist. The specification of just a end key is
 *	  interpretted to mean non-inclusive to that key, i.e. last key
 * 	  in the range will be the key that directly precedes the end key.
 *	  A NULL end key is interpretted as an a key of all 0xFF's up to the
 *	  the maximum length of key for that session, i.e. the last possible
 *	  key.
 * 	o Start inclusive boolean. This boolean allows for the start key to
 * 	  be included in the range.
 * 	o End inclusive boolean. This boolean allows for the end key to
 * 	  be included in the range.
 *	o Count: this designates how many keys are requested or present in the
 * 	  defined range. The number of keys present in the range
 * 	  may less than than the number of possible keys in the defined key
 * 	  space. The number of keys present may also be less than the number
 *	  requested.
 *
 * This structure also contains:
 *  kr_start	Start key. A kio vector representing a single key.
 *  kr_end	End key. A kio vector representing a single key.
 *  kr_flags	Bitmap that encodes the insclusive booleans for start and end
 *  kr_count	Contains requested number of keys in the range.
 *  kr_keys	Actual key list that is represented by the defined
 *		key range 5-tuple above. It is kiovec array where each vector
 *		element is a single key.
 *  kr_keyscnt	Contains the number of keys in the key list, kr_keys.
 */
typedef enum krange_flags {
	/* bitmap enum */
	KRF_NONE      = 0x0000,
	KRF_ISTART    = 0x0001,
	KRF_IEND      = 0x0002,
	KRF_REVERSE   = 0x0100,

	KRF_VALIDMASK = 0x0103,
} krange_flags_t;

typedef struct krange {
	uint32_t       kr_flags;    // Inclusive booleans
	struct kiovec *kr_start;    // Start key
	size_t         kr_startcnt; // kr_start array elemnt count
	struct kiovec *kr_end;      // End key
	size_t         kr_endcnt;   // kr_end array elemnt count
	int32_t        kr_count;    // Num of requested keys in the range
	struct kiovec *kr_keys;     // Key array, one key per vector
	size_t         kr_keyscnt;  // kr_keys array elemnt count

#define KVR_COUNT_INF              (-1)
#define KR_FLAG_SET(_kvr, _kvrf)   ((_kvr)->kr_flags |= (_kvrf))
#define KR_FLAG_CLR(_kvr, _kvrf)   ((_kvr)->kr_flags &= ~(_kvrf))
#define KR_FLAG_ISSET(_kvr, _kvrf) ((_kvr)->kr_flags & (_kvrf))
#define KR_ISTART(_kvr)            ((_kvr)->kr_flags & KRF_ISTART)
#define KR_IEND(_kvr)              ((_kvr)->kr_flags & KRF_IEND)
#define KR_REVERSE(_kvr)           ((_kvr)->kr_flags & KRF_REVERSE)

	// NOTE: currently, this also frees keyrange_data
	void *keyrange_protobuf;
	void (*destroy_protobuf)(struct krange *keyrange_data);
} krange_t;


/**
 * Key Range Iter structure
 *
 * This opaque type permits the iteration through a key a defined keyrange
 *
 */
typedef void kiter_t;


/**
 * Key Batch type
 *
 * This opaque type permits the multi-call batch operation.
 *
 */
typedef void kbatch_t;


/**
 * KIO type
 *
 * This opaque type permits the AIO calls
 *
 */
typedef void kio_t;


/*
 * Kinetic Operation Statistic Structures
 *
 * kop flags are used to control the behavior of opstat structure
 * At this time, operations counts are always performed but time stats
 * are optional.
 */
typedef enum kopflags {
	KOPF_TSTAT	= 0x0001,

#define KIOP_SET(_kop, _kiof)	((_kop)->kop_flags |=  (_kiof))
#define KIOP_CLR(_kop, _kiof)	((_kop)->kop_flags &= ~(_kiof))
#define KIOP_ISSET(_kop, _kiof)	((_kop)->kop_flags &   (_kiof))
} kopflags_t;

typedef struct kopstat {
	uint32_t	kop_flags;

	/* Op Stats
	 * An operation is a get, put, del, ....
	 * An operation is implemented in api call like  ki_get, ki_put
	 *
	 * As all ops are implemented as aio call, errs can occur at anytime
	 * during initiating an op till completing the op. An op is only
	 * considered OK when it is successfully completed. If it fails
	 * it is considered an err.
	 * Drops are when timing is enabled and the collected timestamps
	 * are nonsensical. Zero, negative intervals or intervals > KOP_MAXINT
	 * are dropped. This is due to weirdness in the Linux per cpu clocks.
	 * They are not necessarily sync'd so as threads move from one CPU
	 * to the other intervals may span clocks, creating these nonsensical
	 * intervals.
	 */
	uint32_t 	kop_ok;		/* total cnt */
	uint32_t 	kop_err;	/* total errs */
	uint32_t 	kop_dropped;	/* total dropped time ranges */

	/*
	 *  Sizes - keep a running mean of
	 *  	RPC send / recv size
	 */
	double		kop_ssize;
	double		kop_smsq;
	double		kop_sstdev;
	double		kop_rsize;
	double		kop_klen;
	double		kop_vlen;

	/* Op Time Stats
	 * These stats measure total time time spent in the op
	 * across all calls to that op. A running mean & mean square are kept
	 * so that a sample variance and std deviation can be calculated
	 * later. These time stat operations require a handful of double
	 * precision math operations and time conversion operations.
	 * As these stats are generally not needed during normal operations,
	 * They are disabled vi the kop_flags above and the elements
	 * below remain untouched.
	 */
#define KOP_TTOTAL	0
#define KOP_TMEAN	1
#define KOP_TMEANSQ	2
#define KOP_TVAR	3
#define KOP_TSTDDEV	4
#define KOP_TMAX	5

	double		kop_tot[KOP_TMAX];	/* time spent in an op */
	double		kop_req[KOP_TMAX];	/* time spent in req portion */
	double		kop_resp[KOP_TMAX];	/* time spent in req portion */

	/* Remove me */
#define KOP_TT		0
#define KOP_ST		1
#define KOP_RT		2
#define KOP_TTMAX	3
#define KOP_TTRECORDS	66000
	uint64_t	kop_times[KOP_TTRECORDS][KOP_TTMAX];
} kopstat_t;

typedef struct  kstats {
	kopstat_t 	kst_puts;
	kopstat_t 	kst_dels;
	kopstat_t 	kst_gets;
	kopstat_t 	kst_noops;

#if 0
	kopstat_t 	kst_cbats;	/* Create Batch */
	kopstat_t 	kst_sbats;	/* Submit Batch */
	kopstat_t 	kst_bputs;	/* Batch Puts */
	kopstat_t 	kst_bdels;	/* Batch Deletes */

	kopstat_t 	kst_range;
	kopstat_t 	kst_getlog;
#endif
} kstats_t;


/* ------------------------------
 * Types for interfacing with API
 */
enum kresult_code {
	SUCCESS = 0,
	FAILURE    ,
};

struct kbuffer {
	size_t  len;
	void   *base;
};

struct kresult_buffer {
	enum kresult_code  result_code;
	size_t             len;
	void              *base;
};

struct kresult_message {
	enum kresult_code  result_code;
	void              *result_message;
};

typedef struct kversion {
	const char 	*kvn_pb_c_vers;
	uint32_t 	kvn_pb_c_vers_num;
	char 		*kvn_pb_kinetic_vers;
	const char	*kvn_ki_githash;
	char 		*kvn_ki_vers;
	uint32_t 	kvn_ki_vers_num;
} kversion_t;

#endif /*  _KINETIC_TYPES_H */
