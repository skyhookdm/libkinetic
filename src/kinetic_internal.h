#ifndef _KINETIC_INT_H
#define _KINETIC_INT_H

#include "message.h"
#include "session.h"
#include "kinetic.h"
#include "getlog.h"

/* Abstracting malloc and free, permits testing  */ 
#define KI_MALLOC(_l) malloc((_l))
#define KI_FREE(_p) free((_p))

/* Some utilities */
int ki_validate_kv(kv_t *kv, klimits_t *lim);


#endif /* _KINET_INT_H */
