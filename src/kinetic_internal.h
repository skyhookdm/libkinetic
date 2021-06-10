/**
 * Copyright 2020-2021 Seagate Technology LLC and/or its Affliates
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
#ifndef _KINETIC_INT_H
#define _KINETIC_INT_H

#include <pthread.h>

#include "kinetic.h"
#include "kinetic_types.h"
#include "session.h"
#include "list.h"

/* ------------------------------
 * Constants
 */

/* Abstracting malloc and free, permits testing  */ 
#define UNALLOC_VAL ((void *) 0xDEADCAFE)

#define KI_MALLOC(_l)     malloc((_l))
#define KI_REALLOC(_p,_l) realloc((_p),(_l))
//	debug_printf("KI_FREE(%p)\n", (_p));
#define KI_FREE(_p) {	    \
	free((_p));         \
	(_p) = UNALLOC_VAL; \
}

/* ------------------------------
 * Convenience macros for using generated protobuf code
 */

/* - From protobuf structs to custom structs - */
// extract primitive, optional fields
#define extract_primitive_optional(lvar, proto_struct, field) {		\
	if ((proto_struct)->has_##field) {				\
		lvar = (proto_struct)->field;				\
	}								\
}

// extract bytes (ProtobufCBinaryData) to char * with size
#define extract_bytes_optional(lptr, lsize, proto_struct, field) {	\
	if ((proto_struct)->has_##field) {				\
		lptr  = (proto_struct)->field.data;			\
		lsize = (proto_struct)->field.len;			\
	}								\
}

// extract bytes (ProtobufCBinaryData) to char * (null-terminated string)
#define copy_bytes_optional(lptr, proto_struct, field) {		\
	if ((proto_struct)->has_##field) {				\
		lptr = (char *) KI_MALLOC(				\
			sizeof(char) * ((proto_struct)->field.len + 1)	\
		);							\
									\
		if (lptr != NULL) {					\
			memcpy(						\
				lptr,					\
				(proto_struct)->field.data,		\
				(proto_struct)->field.len		\
			);						\
									\
			lptr[(proto_struct)->field.len] = '\0';		\
		}							\
	}								\
}

/* - From custom structs to protobuf structs - */
// set primitive, optional fields
#define set_primitive_optional(proto_struct, field, rvar) {		\
	(proto_struct)->has_##field = 1;				\
	(proto_struct)->field       = rvar;				\
}

// set bytes (ProtobufCBinaryData) from char * with size
#define set_bytes_optional(proto_struct, field, rptr, rsize) {		\
	(proto_struct)->has_##field = 1;				\
	(proto_struct)->field       = (ProtobufCBinaryData) {		\
		.data = (uint8_t *) rptr,				\
		.len  =             rsize,				\
	};								\
}


// macro for constructing errors concisely
#define kstatus_err(kerror_code, ki_errtype, kerror_detail) (		\
	(kstatus_t) {							\
		.ks_code    = (kerror_code),				\
		.ks_message = (char *) (ki_error_msgs[(ki_errtype)]),	\
		.ks_detail  = (kerror_detail),				\
	}								\
)

#define protobuf_bytelen(proto_struct, field) (				\
	(proto_struct)->has_##field ?					\
	(proto_struct)->field.len					\
	: 0								\
)

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
	pthread_mutex_t	kb_m;		/* Mutex protecting this structure */
#ifdef KBATCH_SEQTRACKING
	LIST		*kb_seqs;	/* the batch ops, perserved as seq# */
#endif
} kb_t;

typedef struct kiter {
	int       ki_ktd;
	krange_t *ki_rreq; 	/* Original Caller Request Range */
	krange_t *ki_rwin1;	/* Range Window 1 */
	krange_t *ki_rwin2;	/* Range Window 2 - UNUSED until AIO */
	krange_t *ki_curwin;	/* Current Range Window */
	uint32_t  ki_curkey;	/* Current key index */
	int32_t   ki_seenkeys;	/* Total keys returned to caller */
	uint32_t  ki_maxkeyreq; /* Max count of keys per request */
	kio_t    *ki_kio;	/* kio for async getrange - UNUSED until AIO */
} ki_t;

/* Some utilities */
size_t calc_total_len(struct kiovec *byte_fragments, size_t fragment_count);

int ki_validate_kv(kv_t *kv, int versck, int valck, klimits_t *lim);
int ki_validate_range(krange_t *kr, klimits_t *lim);
int ki_validate_kb(kb_t *kb, kmtype_t msg_type);
int ki_validate_glog(kgetlog_t *glrq);
int ki_validate_glog2(kgetlog_t *glrq, kgetlog_t *glrsp);
int ki_validate_kstats(kstats_t *kst);
int ki_validate_kapplet(kapplet_t *app, klimits_t *lim);

int b_batch_addop(kb_t *kb, kcmdhdr_t *kc);
kstatus_t b_startbatch(int ktd, kbatch_t *kb);

int s_stats_addts(struct kopstat *kop, struct kio *kio);

#endif /* _KINET_INT_H */
