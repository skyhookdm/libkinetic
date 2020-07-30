#ifndef _KINETIC_INT_H
#define _KINETIC_INT_H

#include "message.h"
#include "session.h"
#include "kinetic.h"
#include "getlog.h"

/* ------------------------------
 * Constants
 */

#define KIO_LEN_NOVAL   2
#define KIO_LEN_WITHVAL 3

/* Abstracting malloc and free, permits testing  */ 
#define KI_MALLOC(_l) malloc((_l))
#define KI_FREE(_p) free((_p))

// NOTE: this is not yet used; just thinking of ways to make code faster to read
/* To shorten inline construction of kstatus_t */
#define inline_kstatus(code, msg, detail) { \
	return (kstatus_t) {                    \
		.ks_code = (code),                  \
		.ks_message = (msg),                \
		.ks_detail = (detail),              \
	};                                      \
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
#define copy_bytes_optional(lptr, proto_struct, field) { \
	if ((proto_struct)->has_##field) {                   \
		memcpy(                                          \
			lptr,                                        \
			(proto_struct)->field.data,                  \
			(proto_struct)->field.len                    \
		);                                               \
	}                                                    \
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


/* Some utilities */
size_t calc_total_len(struct kiovec *byte_fragments, size_t fragment_count);

int ki_validate_kv(kv_t *kv, klimits_t *lim);

char *helper_bytes_to_str(ProtobufCBinaryData proto_bytes);

#endif /* _KINET_INT_H */
