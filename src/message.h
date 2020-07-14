#ifndef _MESSAGE_H
#define _MESSAGE_H
/*
 * Message details user does not need to see
 */
#include "kinetic.h"

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
	uint32_t	kp_vallen;	/* Length of the value */
} kpdu_t; 
#define KP_MAGIC 	0x46
#define KP_LENGTH 	sizeof(kpdu_t)	
#define KP_INIT 	{ KP_MAGIC, 0, 0 }

#define KMAT(ka) COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__##ka
wtypedef enum kauth {
	KA_INVALID	= KMAT(INVALID_AUTH_TYPE),
	KA_HMAC		= KMAT(HMACAUTH),
	KA_PIN		= KMAT(PINAUTH),
	KA_SERVER	= KMAT(UNSOLICITEDSTATUS),
} kauth_t;

#define KPCP(kp) COM__SEAGATE__KINETIC__PROTO__COMMAND__PRIORITY__##kp
typedef enum kpriority {
	LOWEST	= KPCP(LOWEST),
	LOWER	= KPCP(LOWER),
	LOW	= KPCP(LOW),
	LNORMAL	= KPCP(LOWERNORMAL),
	NORMAL	= KPCP(NORMAL),
	HNORMAL	= KPCP(HIGHERNORMAL),
	HIGH	= KPCP(HIGH),
	HIGHER	= KPCP(HIGHER),
	HIGHEST	= KPCP(HIGHEST),
} kpriority_t;

typedef struct kmsghdr {
	kauth_t		kmh_atype;	/* Message Auth Type */
	int64_t		kmh_id;		/* if HMAC, HMAC ID */
	void		*kmh_hmac;	/* if HMAC, HMAC byte string */
	uint32_t	kmh_hmaclen;	/* if HMAC, HMAC length */
	void		*kmh_pin;	/* if PIN, PIN byte string */
	uint32_t	kmh_pinlen;	/* if PIN, PIN Length */
} kmsghdr_t;

typedef struct kcmdhdr {
	int64_t		kch_clustvers;	/* Cluster Version Number */
	int64_t		kch_connid;	/* Connection ID */
	int64_t		kch_seq;	/* Request Sequence Number */
	int64_t		kch_ackseq;	/* Response Sequence Number */
	kmtype_t	kch_type;	/* Request Command Type */
	int64_t		kch_timeout;	/* Timeout Period */
	kpriority_t	kh_pri;		/* Request Priority */
	int64_t		kch_quanta;	/* Time Quanta */
	int32_t		kch_qexit;	/* Boolean: Quick Exit */
	int32_t		kch_batid;	/* Batch ID */
} kcmdhdr_t;

#endif /* _MESSAGE_H */
