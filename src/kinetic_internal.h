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

/* Some utilities */
int ki_validate_kv(kv_t *kv, klimits_t *lim);

char *helper_bytes_to_str(ProtobufCBinaryData proto_bytes);

#endif /* _KINET_INT_H */
