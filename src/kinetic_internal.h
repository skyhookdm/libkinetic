#ifndef _KINETIC_INT_H
#define _KINETIC_INT_H

#include "message.h"
#include "session.h"
#include "kinetic.h"
#include "getlog.h"

/* Abstracting malloc and free, permits testing  */ 
#define KI_MALLOC(_l) malloc((_l))
#define KI_FREE(_p) free((_p))

// macro for extracting optional fields
#define assign_if_set(lvar, field_container, field) { \
	if (field_container->has_##field) {               \
		lvar = field_container->field;                \
	}                                                 \
}

// macro for extracting optional field that is a ProtobufCBinaryData struct
#define assign_if_set_charbuf(lvar, field_container, field) { \
	if (field_container->has_##field) {                       \
		lvar = (char *) field_container->field.data;          \
	}                                                         \
}

/* ------------------------------
 * Convenience macros for using generated protobuf code
 */

/* - From protobuf structs to custom structs - */
// extract primitive, optional fields
#define extract_primitive_optional(lvar, proto_struct, field) { \
	if (proto_struct->has_##field) {                            \
		lvar = proto_struct->field;                             \
	}                                                           \
}

// extract bytes (ProtobufCBinaryData) to char * with size
#define extract_bytes_optional(lptr, lsize, proto_struct, field) { \
	if (proto_struct->has_##field) {                               \
		lptr  = proto_struct->field.data;                          \
		lsize = proto_struct->field.len;                           \
	}                                                              \
}

// extract bytes (ProtobufCBinaryData) to char * (null-terminated string)
#define copy_bytes_optional(lptr, proto_struct, field) { \
	if (proto_struct->has_##field) {                     \
		memcpy(                                          \
			lptr,                                        \
			proto_struct->field.data,                    \
			proto_struct->field.len                      \
		);                                               \
	}                                                    \
}

/* - From custom structs to protobuf structs - */
// set primitive, optional fields
#define set_primitive_optional(proto_struct, field, rvar) { \
	proto_struct->has_##field = 1;                          \
	proto_struct->field       = rvar;                       \
}

// set bytes (ProtobufCBinaryData) from char * with size
#define set_bytes_optional(proto_struct, field, rptr, rsize) { \
	proto_struct->has_##field = 1;                             \
	proto_struct->field       = (ProtobufCBinaryData) {        \
		.data = (uint8_t *) rptr,                              \
		.len  =             rsize,                             \
	};                                                         \
}


/* Some utilities */
int ki_validate_kv(kv_t *kv, klimits_t *lim);

char *helper_bytes_to_str(ProtobufCBinaryData proto_bytes);

#endif /* _KINET_INT_H */
