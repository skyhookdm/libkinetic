#ifndef _MESSAGE_H
#define _MESSAGE_H


/*
 * Message details user does not need to see
 */
// #include "kinetic.h"
#include "protocol_types.h"


// Type aliases
typedef Com__Seagate__Kinetic__Proto__Message kproto_msg_t;

/*
 * This library uses KIO vectors using the following convention.
 * Out bound messages:
 * 	Vector	Contents
 * 	  0	The Kinetic PDU
 *	  1	The packed Kinetic request message
 *	  2	(optional)The value
 * In bound messages:
 *	  0	The Kinetic PDU
 *	  1	The packed Kinetic response message and an option value
 */ 
enum kio_index {
	KIOV_PDU = 0,
	KIOV_MSG = 1,
	KIOV_MSGVAL = 1,
	KIOV_VAL = 2,
};

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

/* Packed length */
#define KP_PLENGTH 9

/* this macro unpacks a Kinetic PDU from a sequential set of KP_LENGTH bytes */
#define UNPACK_PDU(_pdu, _p)						    \
	do {								    \
		(_pdu)->kp_magic  = (char)(_p)[0];			    \
		(_pdu)->kp_msglen = (uint32_t)((_p)[4]<<24 | (_p)[3]<<16 |  \
					       (_p)[2]<< 8 | (_p)[1]      );\
		(_pdu)->kp_msglen = ntohl((_pdu)->kp_msglen);		    \
		(_pdu)->kp_vallen = (uint32_t)((_p)[8]<<24 | (_p)[7]<<16 |  \
					       (_p)[6]<< 8 | (_p)[5]      );\
		(_pdu)->kp_vallen = ntohl((_pdu)->kp_vallen);		    \
	} while(0);

/* this macro packs a Kinetic PDU into a sequential set of KP_LENGTH bytes */
#define PACK_PDU(_pdu, _p)						    \
	do {								    \
		uint32_t d;						    \
		(_p)[0] = (char)(_pdu)->kp_magic;			    \
		d = htonl((_pdu)->kp_msglen);				    \
		(_p)[4] = (d & 0xff000000)>>24;				    \
		(_p)[3] = (d & 0x00ff0000)>>16;				    \
		(_p)[2] = (d & 0x0000ff00)>> 8;				    \
		(_p)[1] = (d & 0x000000ff);				    \
		d = htonl((_pdu)->kp_vallen);				    \
		(_p)[8] = (d & 0xff000000)>>24;				    \
		(_p)[7] = (d & 0x00ff0000)>>16;				    \
		(_p)[6] = (d & 0x0000ff00)>> 8;				    \
		(_p)[5] = (d & 0x000000ff);				    \
	} while(0);

// Authorization types (`AuthType`)
#define KMAT(ka) COM__SEAGATE__KINETIC__PROTO__MESSAGE__AUTH_TYPE__##ka
typedef Com__Seagate__Kinetic__Proto__Message__AuthType kauth_t;
enum {
	KA_INVALID     = KMAT(INVALID_AUTH_TYPE),
	KA_HMAC        = KMAT(HMACAUTH)			,
	KA_PIN         = KMAT(PINAUTH)			,
	KA_UNSOLICITED = KMAT(UNSOLICITEDSTATUS),
};


#define KPCP(kp) COM__SEAGATE__KINETIC__PROTO__COMMAND__PRIORITY__##kp
typedef Com__Seagate__Kinetic__Proto__Command__Priority kpriority_t;
enum {
	LOWEST	= KPCP(LOWEST),
	LOWER	= KPCP(LOWER),
	LOW	= KPCP(LOW),
	LNORMAL	= KPCP(LOWERNORMAL),
	NORMAL	= KPCP(NORMAL),
	HNORMAL	= KPCP(HIGHERNORMAL),
	HIGH	= KPCP(HIGH),
	HIGHER	= KPCP(HIGHER),
	HIGHEST	= KPCP(HIGHEST),
};


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
	kpriority_t	kch_pri;		/* Request Priority */
	int64_t		kch_quanta;	/* Time Quanta */
	int32_t		kch_qexit;	/* Boolean: Quick Exit */
	int32_t		kch_batid;	/* Batch ID */
} kcmdhdr_t;


/* ------------------------------
 * General protocol API
 */

struct kresult_message create_message(kmsghdr_t *msg_hdr, ProtobufCBinaryData cmd_bytes);
struct kresult_message unpack_kinetic_message(void *response_buffer, size_t response_size);

enum kresult_code pack_kinetic_message(kproto_msg_t *msg_data, void **msg_buffer, size_t *msg_size);

void destroy_message(void *unpacked_msg);

#endif /* _MESSAGE_H */
