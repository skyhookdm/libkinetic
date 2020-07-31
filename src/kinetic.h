#ifndef _KINETIC_H
#define _KINETIC_H

#include "protocol/kinetic.pb-c.h"
#include "protocol_types.h"
#include "getlog.h"

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

typedef struct keyrange {
	struct kiovec  *kr_startkey;
	size_t          kr_startkeycnt;

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
} batch_t;


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

struct kiovec {
	size_t  kiov_len;     /* Number of bytes to transfer */
	void   *kiov_base;    /* Starting address */
};


/**
 * The API.
 */
int ki_open(char *host, char *port, uint32_t usetls, int64_t id, char *hmac);
int ki_close(int ktd);

kstatus_t ki_setclustervers(int ktd, int64_t vers);

kstatus_t ki_put(int ktd, kv_t *key);
kstatus_t ki_cas(int ktd, kv_t *key);

kstatus_t ki_get(int ktd, kv_t *key);
kstatus_t ki_getnext(int ktd, kv_t *key, kv_t *next);
kstatus_t ki_getprev(int ktd, kv_t *key, kv_t *prev);
kstatus_t ki_getversion(int ktd, kv_t *key);

kstatus_t ki_range(int ktd, kr_t *keyrange);

kstatus_t ki_getlog(int ktd, kgetlog_t *glog);


#endif /*  _KINETIC_H */
