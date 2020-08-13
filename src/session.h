#ifndef _SESSION_H
#define _SESSION_H


/*
 * Session details user does not need to see
 */

#include "message.h"
#include "getlog.h"

typedef struct ksession {
	kbid_t			ks_bid;		/* Next Session Batch ID */
	uint32_t		ks_bats;	/* Active Batches */
	klimits_t		ks_l;		/* Preserved session limits */
	kconfiguration_t	ks_conf;
	kcmdhdr_t		ks_ch;		/* Preserved cmdhdr limits */	
} ksession_t;

#endif /* _SESSION_H */
