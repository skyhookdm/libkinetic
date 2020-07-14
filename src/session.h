#ifndef _MESSAGE_H
#define _MESSAGE_H
/*
 * Session details user does not need to see
 */
#include "kinetic.h"

typedef struct ksession {
	klimits_t	ks_l;
	kcmdhdr_t	ks_ch;		
} ksession_t;

#endif /* _MESSAGE_H */
