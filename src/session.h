#ifndef _SESSION_H
#define _SESSION_H


/*
 * Session details user does not need to see
 */

#include "message.h"
#include "getlog.h"

typedef struct ksession {
	klimits_t		ks_l;
	kconfiguration_t	ks_conf;
	kcmdhdr_t		ks_ch;		
} ksession_t;

#endif /* _SESSION_H */
