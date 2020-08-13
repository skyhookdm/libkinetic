#ifndef _KINETIC_INT_H
#define _KINETIC_INT_H

#include <pthread.h>
#include "kinetic.h"
#include "message.h"
#include "session.h"
#include "getlog.h"
#include "list.h"

/* ------------------------------
 * Constants
 */

/* Abstracting malloc and free, permits testing  */ 
#define UNALLOC_VAL ((void *) 0xDEADCAFE)

#define KI_MALLOC(_l) malloc((_l))
#define KI_REALLOC(_p,_l) realloc((_p),(_l))
#define KI_FREE(_p) {	    \
	free((_p));         \
	(_p) = UNALLOC_VAL; \
}

/* ------------------------------
 * Convenience macros for using generated protobuf code
 */

/* - From protobuf structs to custom structs - */
// extract primitive, optional fields
#define extract_primitive_optional(lvar, proto_struct, field) { \
	if ((proto_struct)->has_##field) {                          \
		lvar = (proto_struct)->field;                           \
	}                                                           \
}

// extract bytes (ProtobufCBinaryData) to char * with size
#define extract_bytes_optional(lptr, lsize, proto_struct, field) { \
	if ((proto_struct)->has_##field) {                             \
		lptr  = (proto_struct)->field.data;                        \
		lsize = (proto_struct)->field.len;                         \
	}                                                              \
}

// extract bytes (ProtobufCBinaryData) to char * (null-terminated string)
#define copy_bytes_optional(lptr, proto_struct, field) {   \
	if ((proto_struct)->has_##field) {                     \
		lptr = (char *) KI_MALLOC(                         \
			sizeof(char) * ((proto_struct)->field.len + 1) \
		);                                                 \
                                                           \
		if (lptr != NULL) {                                \
			memcpy(                                        \
				lptr,                                      \
				(proto_struct)->field.data,                \
				(proto_struct)->field.len                  \
			);                                             \
														   \
			lptr[(proto_struct)->field.len] = '\0';        \
		}                                                  \
	}                                                      \
}

/* - From custom structs to protobuf structs - */
// set primitive, optional fields
#define set_primitive_optional(proto_struct, field, rvar) { \
	(proto_struct)->has_##field = 1;                        \
	(proto_struct)->field       = rvar;                     \
}

// set bytes (ProtobufCBinaryData) from char * with size
#define set_bytes_optional(proto_struct, field, rptr, rsize) { \
	(proto_struct)->has_##field = 1;                           \
	(proto_struct)->field       = (ProtobufCBinaryData) {      \
		.data = (uint8_t *) rptr,                              \
		.len  =             rsize,                             \
	};                                                         \
}

// macro for constructing errors concisely
#define kstatus_err(kerror_code, ki_errtype, kerror_detail) ( \
	(kstatus_t) {                                             \
		.ks_code    = (kerror_code)  ,                        \
		.ks_message = (char *) (ki_error_msgs[(ki_errtype)]), \
		.ks_detail  = (kerror_detail),                        \
	}                                                         \
)


// some forward declarations of what's in kerrors.c
extern const char *ki_error_msgs[];
enum ki_error_type {
	KI_ERR_NOMSG   = 0, /* 0x00  0 */
	KI_ERR_MALLOC     ,
	KI_ERR_BADSESS    ,
	KI_ERR_INVARGS    ,
	KI_ERR_MSGPACK    ,
	KI_ERR_MSGUNPACK  , /* 0x05  5 */
	KI_ERR_CMDUNPACK  ,
	KI_ERR_NOCMD      ,
	KI_ERR_CREATEREQ  ,
	KI_ERR_RECVMSG    ,
	KI_ERR_RECVPDU    , /* 0x10 10 */
	KI_ERR_PDUMSGLEN  ,
	KI_ERR_BATCH      ,
};

/**
 * This is the internal structure for managing a batch across many API calls.
 * It is meant to be completely opaque to the caller. The kbatch_t type
 * in kinetic.h is just a void and all APIs use a kbatch_t pointer (void *). 
 * These caller provided ptrs will be cast kb_t ptrs internally.
 */  
typedef struct kb {
	int		kb_ktd;
	kbid_t		kb_bid;		/* Batch ID */
	uint32_t	kb_ops;		/* Batch Ops count */
	uint32_t	kb_dels;	/* Batch Delete Ops count */
	uint32_t	kb_bytes;	/* Batch total bytes */ 
	LIST		*kb_seqs;	/* the batch ops, perserved as seq# */
	pthread_mutex_t  kb_m;		/* mutex protecting this structure */
} kb_t;

/* Some utilities */
size_t calc_total_len(struct kiovec *byte_fragments, size_t fragment_count);

int ki_validate_kv(kv_t *kv, int force, klimits_t *lim);
int ki_validate_range(krange_t *kr, klimits_t *lim);
int ki_validate_glog(kgetlog_t *glrq);
int ki_validate_glog2(kgetlog_t *glrq, kgetlog_t *glrsp);

int b_batch_addop(kb_t *kb, kcmdhdr_t *kc);

char *helper_bytes_to_str(ProtobufCBinaryData proto_bytes);

#endif /* _KINET_INT_H */
