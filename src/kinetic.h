#ifndef _KINETIC_H
#define _KINETIC_H


#include "protocol_types.h"
#include "message.h"


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
	KDI_INVALID = CA(INVALID_ALGORITHM),
	KDI_SHA1	= CA(SHA1)			   ,
	KDI_SHA2	= CA(SHA2)			   ,
	KDI_SHA3	= CA(SHA3)			   ,
	KDI_CRC32C	= CA(CRC32C)		   ,
	KDI_CRC64	= CA(CRC64)			   ,
	KDI_CRC32	= CA(CRC32)			   ,
};


// Kinetic Status Codes
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
	size_t			   len;
	void			  *base;
};

struct kresult_message {
	enum kresult_code  result_code;
	void			  *result_message;
};

// Kinetic Status block
typedef struct kstatus {
	kstatus_code_t	ks_code;
	char		   *ks_message;
	char		   *ks_detail;
} kstatus_t;


/**
 * The API.
 */
int ki_open(char *host, char *port, uint32_t usetls, int64_t id, char *hmac);
int ki_close(int ktd);

kstatus_t ki_setclustervers(int ktd, int64_t vers);

kstatus_t ki_get(int ktd, char *key, void **value,
				 char **vers, char **tag, kditype_t **di);

kstatus_t ki_getlog(int ktd, kgetlog_t *glog);


#endif /*  _KINETIC_H */
