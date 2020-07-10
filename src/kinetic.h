#ifndef _KINETIC_H
#define _KINETIC_H 

#include ""

/**
 * Kinetic PDU structure
 * The pdu structure is the first bits on the wire and defines the total
 * length of the elements that follow.  The first element is a magic byte
 * that defines the beginning of the PDU - it is set to 'F' or 0x46, then 
 * the length of the kinetic protobuf message and then finally the length 
 * of the value, if any. 
 *
 *        +------------------------------------------------+
 *        |  Kinetic Magic   - Must be 'F' 0x46            |
 *        +------------------------------------------------+
 *        |  Protobuf Length - BE, Bounded 0 <  L <= 1024k |
 *        +------------------------------------------------+
 *        |  Value Length    - BE, Bounded 0 <= L <= 1024k |
 *        +------------------------------------------------+
 */
typedef struct kpdu {
	uint8_t		kp_magic;	/* Always 'F' 0x46 */
	uint32_t	kp_msglen;	/* Length of protobuf message */
	uint32_t	kp_vallen;	/* Length of the value *
} kpdu_t; 
#define KP_MAGIC 0x46
#define KP_LENGTH sizeof(pdu_t)	
#define KP_INIT { KP_MAGIC, 0, 0 }

/** 
 * Kinetic Status Codes 
 */ 
#define CSSC(cssc) Command_Status_StatusCode_##cssc
typedef enum kstatus_code {
	K_INVALID_SC	= CSSC(INVALID_STATUS_CODE),
	K_OK		= CSSC(SUCCESS),
	K_EREJECTED	= CSSC(NOT_ATTEMPTED),
	K_EHMAC		= CSSC(HMAC_FAILURE),
	K_EACCESS	= CSSC(NOT_AUTHORIZED),
	K_EVERSION	= CSSC(VERSION_FAILURE),
	K_EINTERNAL	= CSSC(INTERNAL_ERROR),
	K_ENOHEADER	= CSSC(HEADER_REQUIRED),
	K_ENOTFOUND 	= CSSC(NOT_FOUND),
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
	K_EHIBERNATE	= CSSC(HIBERNATE),
	K_ESHUTDOWN	= CSSC(SHUTDOWN),
} kstatus_code_t;

/**
 * Kinetic Status block
 */ 
typedef struct kstatus {
	kstatus_code_t	ks_code;
	char *ks_message;
	char *ks_detail;
} kstatus_t;	

/**
 * Kinetic Data Integrity supported algrithms.
 */
#define CA(ca) Command_Algorithm_##ca
enum kdi {
	KDI_INVALID	= CA(INVALID_ALGORITHM),
	KDI_SHA1	= CA(SHA1),
	KDI_SHA2	= CA(SHA2),
	KDI_SHA3	= CA(SHA3),
	KDI_CRC32C	= CA(CRC32C),
	KDI_CRC64	= CA(CRC64),
	KDI_CRC32	= CA(CRC32).
};

/**
 * Kinetic Message Types, ie Kinetic Ops
 */
#define CMT(cmt) Command_MessageType_##cmt
typedef enum kmtype {
	KMT_INVALID	= CMT(INVALID_MESSAGE_TYPE),
	KMT_GET		= CMT(GET),		   
	KMT_PUT		= CMT(PUT),		   
	KMT_DEL		= CMT(DELETE),	   
	KMT_GETNEXT 	= CMT(GETNEXT),	   
	KMT_GETPREV	= CMT(GETPREVIOUS),	   
	KMT_GETRANGE	= CMT(GETKEYRANGE),	   
	KMT_GETVERS 	= CMT(GETVERSION),	   
	KMT_SETUP 	= CMT(SETUP),	   
	KMT_GETLOG 	= CMT(GETLOG),	   
	KMT_SECURITY 	= CMT(SECURITY),	   
	KMT_PUSHP2P 	= CMT(PEER2PEERPUSH),  
	KMT_NOOP	= CMT(NOOP),	   
	KMT_FLUSH	= CMT(FLUSHALLDATA),   
	KMT_PINOP	= CMT(PINOP),	   
	KMT_SCANMEDIA 	= CMT(MEDIASCAN),	   
	KMT_OPTMEDIA 	= CMT(MEDIAOPTIMIZE),  
	KMT_STARTBAT	= CMT(START_BATCH),	   
	KMT_ENDBAT 	= CMT(END_BATCH),	   
	KMT_ABORTBAT	= CMT(ABORT_BATCH),	   
	KMT_SETPOWER	= CMT(SET_POWER_LEVEL),
} kmtype_t;

/**
 * Kinetic Cache Policies
 */
#define CS(cs) Command_Synchronization_##cs
typedef enum kcachepolicy {
	KC_INVALID 	= CS(INVALID_SYNCHRONIZATION),
	KC_WT 		= CS(WRITETHROUGH),	 
	KC_WB		= CS(WRITEBACK),	 
	KC_FLUSH	= CS(FLUSH),
} kcachepolicy_t;

/**
 * Kinetic Server Power Levels, 
 * PAK: probably should be in management.h
 */
#define CPL(cpl) Command_PowerLevel_##cpl
typedef enum kpowerlevel {
	KPL_INVALID	= CPL(INVALID_LEVEL),
	KPL_NOMINAL 	= CPL(OPERATIONAL), 
	KPL_HIBERNATE	= CPL(HIBERNATE),
	KPL_SHUTDOWN	= CPL(SHUTDOWN),
	KPL_FAIL	= CPL(FAIL),		
} kpowerlevel_t;

/* Abstracting malloc and free, permits testing  */ 
#define KI_MALLOC(_l) malloc((_l))
#define KI_FREE(_p)   free((_p))

kstatus_t ki_setclustervers(int ktd, int64_t vers);

kstatus_t ki_get(int ktd, char *key, void **value,
	      char **vers, char **tag, kdi_t **di);

kstatus_t ki_getlog(int ktd, kgetlog_t *glog);
	
#endif /*  _KINETIC_H */

