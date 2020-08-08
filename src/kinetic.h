#ifndef _KINETIC_H
#define _KINETIC_H

#include "protocol/kinetic.pb-c.h"
#include "protocol_types.h"
#include "getlog.h"

/* macros for printing/logging */

// define log levels
#define LOGLEVEL_NONE  0
#define LOGLEVEL_INFO  1
#define LOGLEVEL_DEBUG 2

// this is the log level set for the program
#define LOGLEVEL LOGLEVEL_DEBUG

// macros that use the log level
#if LOGLEVEL >= LOGLEVEL_DEBUG
	#define debug_fprintf(...) fprintf(__VA_ARGS__)
#else
	#define debug_fprintf(...) {}
#endif

#if LOGLEVEL >= LOGLEVEL_INFO
	#define info_fprintf(...) fprintf(__VA_ARGS__)
#else
	#define info_fprintf(...) {}
#endif


// printf's as an alias to fprintf's for convenience
#define debug_printf(...) debug_fprintf(stdout, __VA_ARGS__)
#define info_printf(...)  info_fprintf(stdout, __VA_ARGS__)

//Forward declarations of operation-specific structs used in the API
enum header_field_type;

struct kgetlog;
typedef struct kgetlog kgetlog_t;

/* ------------------------------
 * Type aliases for protocol
 */

// Kinetic Data Integrity supported algrithms.
#define CA(ca) COM__SEAGATE__KINETIC__PROTO__COMMAND__ALGORITHM__##ca

typedef Com__Seagate__Kinetic__Proto__Command__Algorithm kditype_t;
enum {
	KDI_INVALID	= CA(INVALID_ALGORITHM),
	KDI_SHA1	= CA(SHA1)		   ,
	KDI_SHA2	= CA(SHA2)		   ,
	KDI_SHA3	= CA(SHA3)		   ,
	KDI_CRC32C	= CA(CRC32C)	   ,
	KDI_CRC64	= CA(CRC64)		   ,
	KDI_CRC32	= CA(CRC32)		   ,
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


// Kinetic Status Codes
#define CSSC(cssc) COM__SEAGATE__KINETIC__PROTO__COMMAND__STATUS__STATUS_CODE__##cssc

typedef Com__Seagate__Kinetic__Proto__Command__Status__StatusCode kstatus_code_t;
enum {
	K_INVALID_SC	= CSSC(INVALID_STATUS_CODE),
	K_OK		= CSSC(SUCCESS),
	K_EREJECTED	= CSSC(NOT_ATTEMPTED),
	K_EHMAC		= CSSC(HMAC_FAILURE),
	K_EACCESS	= CSSC(NOT_AUTHORIZED),
	K_EVERSION	= CSSC(VERSION_FAILURE),
	K_EINTERNAL	= CSSC(INTERNAL_ERROR),
	K_ENOHEADER	= CSSC(HEADER_REQUIRED),
	K_ENOTFOUND	= CSSC(NOT_FOUND),
	K_EBADVERS	= CSSC(VERSION_MISMATCH),
	K_EBUSY		= CSSC(SERVICE_BUSY),
	K_ETIMEDOUT	= CSSC(EXPIRED),
	K_EDATA		= CSSC(DATA_ERROR),
	K_EPERMDATA	= CSSC(PERM_DATA_ERROR),
	K_EP2PCONN	= CSSC(REMOTE_CONNECTION_ERROR),
	K_ENOSPACE	= CSSC(NO_SPACE),
	K_ENOHMAC	= CSSC(NO_SUCH_HMAC_ALGORITHM),
	K_EINVAL	= CSSC(INVALID_REQUEST),
	K_EP2P		= CSSC(NESTED_OPERATION_ERRORS),
	K_ELOCKED	= CSSC(DEVICE_LOCKED),
	K_ENOTLOCKED	= CSSC(DEVICE_ALREADY_UNLOCKED),
	K_ECONNABORTED	= CSSC(CONNECTION_TERMINATED),
	K_EINVALBAT	= CSSC(INVALID_BATCH),
	K_EHIBERNATE   	= CSSC(HIBERNATE),
	K_ESHUTDOWN	= CSSC(SHUTDOWN),
};

/**
 * koivec structure
 *
 * The kiovec structure originally was a KTLI structure
 * used to pass logically assembled buffers to the transport layer 
 * without copying them into a single contiguous buffer. It is analogous to
 * Linux's iovec structure.  In libkinetic, the kiovec is also used to move
 * around keys and values that may be assembled from multiple parts. 
 */
struct kiovec {
	size_t  kiov_len;     /* Number of bytes */
	void   *kiov_base;    /* Starting address */
};

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
	struct kiovec	*kv_key;
	size_t		kv_keycnt;
	struct kiovec	*kv_val;
	size_t		kv_valcnt;
	void		*kv_ver;
	size_t		kv_verlen;
	void		*kv_newver;
	size_t		kv_newverlen;
	void		*kv_disum;
	size_t		kv_disumlen;
	kditype_t	kv_ditype;
	kcachepolicy_t	kv_cpolicy;
	
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
	KRF_NONE	= 0x0000,
	KRF_ISTART 	= 0x0001,
	KRF_IEND	= 0x0002,
	KRF_REVERSE	= 0x0100,
	
	KRF_VALIDMASK	= 0x0103, 
} krange_flags_t;

typedef struct krange {
	uint32_t	kr_flags;	/* Inclusive booleans */
	struct kiovec	*kr_start;	/* Start key */ 
	size_t		kr_startcnt;	/* kr_start array elemnt count */
	struct kiovec	*kr_end;	/* End key */
	size_t		kr_endcnt;	/* kr_end array elemnt count */
	int32_t		kr_count;	/* Num of requested keys in the range */
	struct kiovec	*kr_keys;	/* Key array, one key per vector */
	size_t		kr_keyscnt;	/* kr_keys array elemnt count */
#define KVR_COUNT_INF			(-1)
#define KR_FLAG_SET(_kvr, _kvrf)	((_kvr)->kr_flags |= (_kvrf))
#define KR_FLAG_CLR(_kvr, _kvrf)	((_kvr)->kr_flags &= ~(_kvrf))
#define KR_FLAG_ISSET(_kvr, _kvrf)	((_kvr)->kr_flags & (_kvrf))
#define KR_ISTART(_kvr)			((_kvr)->kr_flags & KRF_ISTART)
#define KR_IEND(_kvr)			((_kvr)->kr_flags & KRF_IEND)
#define KR_REVERSE(_kvr)		((_kvr)->kr_flags & KRF_REVERSE)

	// NOTE: currently, this also frees keyrange_data
	void        *keyrange_protobuf;
	void        (*destroy_protobuf)(struct krange *keyrange_data);
} krange_t;

/**
 * Key Range Iter structure
 *
 * This structure permits the iteration through a key a defined keyrange
 * 
 */
typedef struct kiter {
	int		ki_ktd;
	krange_t	*ki_range;
	uint32_t	ki_curr;	/* Current key index */
	uint32_t	ki_maxkeyreq;	/* Max count of keys per request */
	int32_t		ki_count;	/* Total keys requested, could be INF */
	struct kiovec   *ki_boundary;	/* Last key special case, always cnt=1, 
					   see ki_iternext() comment */
} kiter_t;

#if 0
typedef struct keyrange {
	struct kiovec  *start_key;
	size_t          start_keycnt;

	struct kiovec  *kr_endkey;
	size_t          kr_endkeycnt;

	int             kr_bool_is_start_inclusive;
	int             kr_bool_is_end_inclusive;
	int             kr_bool_reverse_keyorder;

	uint32_t        kr_max_keylistcnt;

	// result_keys is an array of keys, where each iovec holds a single, contiguous key name
	struct kiovec *kr_result_keylist;
	size_t         kr_result_keylistcnt;

	// NOTE: currently, this also frees keyrange_data
	void        *keyrange_protobuf;
	void        (*destroy_protobuf)(struct keyrange *keyrange_data);
} kr_t;

// TODO
typedef struct batch {
} kb_t;

#endif

/* ------------------------------
 * Types for interfacing with API
 */
enum kresult_code {
	SUCCESS = 0,
	FAILURE    ,
};

struct kbuffer {
	size_t	len;
	void   *base;
};

struct kresult_buffer {
	enum kresult_code  result_code;
	size_t		   len;
	void		  *base;
};

struct kresult_message {
	enum kresult_code  result_code;
	void		  *result_message;
};

// Kinetic Status block
typedef struct kstatus {
	kstatus_code_t	ks_code;
	char		*ks_message;
	char		*ks_detail;
} kstatus_t;

/**
 * The API.
 */
int ki_open(char *host, char *port, uint32_t usetls, int64_t id, char *hmac);
int ki_close(int ktd);

klimits_t ki_limits(int ktd);

kstatus_t ki_setclustervers(int ktd, int64_t vers);

kstatus_t ki_put(int ktd, kv_t *key);
kstatus_t ki_cas(int ktd, kv_t *key);

kstatus_t ki_del(int ktd, kv_t *key);
kstatus_t ki_cad(int ktd, kv_t *key);

kstatus_t ki_get(int ktd, kv_t *key);
kstatus_t ki_getnext(int ktd, kv_t *key, kv_t *next);
kstatus_t ki_getprev(int ktd, kv_t *key, kv_t *prev);
kstatus_t ki_getversion(int ktd, kv_t *key);

kstatus_t ki_range(int ktd, krange_t *kr);

kstatus_t ki_getlog(int ktd, kgetlog_t *glog);

kiter_t *ki_itercreate(int ktd);
int      ki_iterfree(kiter_t *kit);
int      ki_iterdone(kiter_t *kit);
struct kiovec *ki_iterstart(kiter_t *kit, krange_t *kr);
struct kiovec *ki_iternext(kiter_t *kit);

int ki_keyfree(struct kiovec *key, size_t keycnt);
struct kiovec *ki_keycreate(void *keybuf, size_t keylen);	
struct kiovec *ki_keyprefix(struct kiovec *key, size_t keycnt,
			    void *keybuf, size_t keylen);
struct kiovec *ki_keypostfix(struct kiovec *key, size_t keycnt,
			     void *keybuf, size_t keylen);	
struct kiovec *ki_keydup(struct kiovec *key, size_t keycnt);	
struct kiovec *ki_keydupf(struct kiovec *key, size_t keycnt);	
struct kiovec *ki_keyfirst();
struct kiovec *ki_keylast(size_t len);

krange_t *ki_rangedup(krange_t *kr);
int ki_rangefree(krange_t *kr);

#endif /*  _KINETIC_H */
