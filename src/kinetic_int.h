#ifndef _KINETIC_INT_H
#define _KINETIC_INT_H

#include "message.h"
#include "session.h"

/* Abstracting malloc and free, permits testing  */ 
#define KI_MALLOC(_l) malloc((_l))
#define KI_FREE(_p) free((_p))


#endif /* _KINET_INT_H */
