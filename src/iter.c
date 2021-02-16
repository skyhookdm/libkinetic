/**
 * Copyright 2020-2021 Seagate Technology LLC.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"

/*
 * The kinetic iterator. 
 * An iterator must be created (ki_create) before it can be used. Once 
 * created it can be used many times before it is destroyed (ki_destroy). 
 * Although a given iterator can be used many times, it can not be shared
 * and used simultaneously.
 *
 * An iterator is started by calling ki_iterstart with a valid range to 
 * iterate though. The provided range is copied for internal use. As for the
 * range, the kinetic iterator honors start and end keys, but also
 * honors the abscence of these keys.  If these keys not provided, the first 
 * legal key and last legal key are substituted. The Kinetic iterator also 
 * honors the inclusive flags for both the start and end keys. Key counts
 * from 1 to infinity are also supported.  NOTE: Currently reverse is NOT
 * supported. Successive calls to ki_iterstart reset the iterator to the 
 * new range provided. ki_iterstart always returns the first key. 
 *
 * The boolean ki_iterdone must always be called on each iteration to check 
 * all boundary conditions. If true the iteration is complete. 
 *
 * ki_iternext returns the next key in the sequence. 
 *
 * An example would be:
 *	struct kiovec   *k;
 *	krange_t	kr;
 *	kiter_t		*kit;
 *
 *	for (k = ki_iterstart(kit, &kr); 
 * 	     !ki_iterdone(kit) && k;  k = ki_iternext(kit)) {
 *	}
 */

/* This iscalled by ki_create to init the kiter */
int
i_iterinit(int ktd, kiter_t *kit)
{
	int rc;
	ksession_t *ses;
	struct ktli_config *cf;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		return(rc);
	}
	ses = (ksession_t *)cf->kcfg_pconf;

	kit->ki_ktd   = ktd;
	kit->ki_range = NULL;
	kit->ki_curr  = 0;
	kit->ki_count = 0;
	kit->ki_maxkeyreq = ses->ks_l.kl_rangekeycnt;
	kit->ki_boundary = NULL;
	
	return(0);
}

/* this is called by ki_destroy */
void
i_iterdestroy(kiter_t *kit)
{
	krange_t * kr;
	if (!kit)
		return;

	kr = kit->ki_range;
	
	if (kit->ki_boundary) {
		/* 
		 * corner case when a multiple ki_range iter presents an even 
		 * multiple of the ki_maxkeyreq number of keys. Normally the 
		 * boundary key is freed after ki_range call boundary is
		 * crossed. if its hit but never crosses we miss the free. 
		 * handle it here. 
		 */
		ki_keydestroy(kit->ki_boundary, 1);
	}

	ki_keydestroy(kr->kr_keys,  kr->kr_keyscnt);
	ki_keydestroy(kr->kr_start, kr->kr_startcnt);
	ki_keydestroy(kr->kr_end,   kr->kr_endcnt);
	ki_destroy(kr);	
	return;
}

struct kiovec *
ki_start(kiter_t *kit, krange_t *kr)
{
	kstatus_t krc; 
	if (!kit || !kr || !kr->kr_count)
		return(NULL);

	/* Cleanup the old range and boundary keys if any */
	ki_destroy(kit->ki_range);
	ki_keydestroy(kit->ki_boundary, 1);
	kit->ki_boundary = NULL;

	/* Copy the passed in range */
	kit->ki_range = ki_rangedup(kit->ki_ktd, kr);
	if (!kit->ki_range)
		return(NULL);

	/* Start at the 0th key */
	kit->ki_curr = 0;

	/* Do NOT change the inclusive flags use as is from caller */
	
	/* for the iterator we always try to grab the most keys as possible */
	if (kit->ki_range->kr_count < 0) {
		/* Infinity - all keys in range */
		kit->ki_range->kr_count = KVR_COUNT_INF;
		kit->ki_count = KVR_COUNT_INF;
	} else if (kit->ki_range->kr_count <= kit->ki_maxkeyreq) {
		/* Less keys than a single ki_range call can provide */
		kit->ki_count = kit->ki_range->kr_count;
	} else {
		/* Multiple ki_range calls needed, so so large as possible */
		kit->ki_count = kit->ki_range->kr_count;
		kit->ki_range->kr_count = kit->ki_maxkeyreq;
	}

	/* Load the first batch of keys */
	krc = ki_getrange(kit->ki_ktd, kit->ki_range);
	if (krc !=  K_OK)
		return(NULL);

	/* for an iter of size 1 */
	if (kit->ki_curr == (kit->ki_range->kr_keyscnt - 1)) {
		/* Last key - handle boundary condition */
		kit->ki_boundary = ki_keydupf(&kit->ki_range->kr_keys[kit->ki_curr], 1);
		if (!kit->ki_boundary) {
			return(NULL);
		}
		if (kit->ki_count > 0) kit->ki_count--;
		return(kit->ki_boundary);
	}

	if (kit->ki_count > 0) kit->ki_count--;
	return(&kit->ki_range->kr_keys[kit->ki_curr]);
}

int
ki_done(kiter_t *kit)
{
	kstatus_t krc;
	krange_t *kr;

	if (!kit || !kit->ki_range)
		/* error = done */
		return(1);

	/* shorthand var */
	kr = kit->ki_range;

	if (!kr->kr_keyscnt) {
		/* 
		 * Last range call to fill the cache came up empty, 
		 * so the iter is done
		 */
		return(1);
	}
	
	kit->ki_curr++;
	if (kit->ki_curr < kr->kr_keyscnt) {
		/* More keys available = not done */
		return(0);
	}

	/* 
	 * no more keys in current range cache, need to adjust the 
	 * range and refill the cache
	 *
	 *  Move last key in cache to new start key
	 */
	ki_keydestroy(kr->kr_start,  kr->kr_startcnt);
	kr->kr_start    = ki_keydupf(&kr->kr_keys[kit->ki_curr-1], 1);
	kr->kr_startcnt = 1;

	/* Need to clear the inclusive start field as to not repeat keys */
	KR_FLAG_CLR(kr, KRF_ISTART);

	/* Now that we have copied the last key, free the existing cache */
	ki_keydestroy(kr->kr_keys,  kr->kr_keyscnt);
	kr->kr_keys    = NULL;
	kr->kr_keyscnt = 0;

	/* for the iterator we always try to grab the most keys as possible */
	if (kit->ki_count < 0) {
		/* Infinity - all keys in range */
		kr->kr_count  = KVR_COUNT_INF;
		kit->ki_count = KVR_COUNT_INF;
	} else if (kit->ki_count <= kit->ki_maxkeyreq) {
		/* Ask for what's left with a single ki_range call*/
		kr->kr_count = kit->ki_count;
		printf("Sub range call: %d\n",  kit->ki_count);
	} else {
		/* Multiple ki_range calls needed, so large as possible */
		kr->kr_count = kit->ki_maxkeyreq;
	}

	/* Load the next batch of keys */
	krc = ki_getrange(kit->ki_ktd, kr);
	if (krc != K_OK)
		/* no more keys to get = done */
		return(1);

	/* kr_keyscnt could be 0 from a successful range call 
	   catch next time through */
	
	/* reset curr for this new batch */
	kit->ki_curr = 0; 

	return(0); /* not done */
}

struct kiovec *
ki_next(kiter_t *kit)
{
	krange_t *kr;
	
	if (!kit || !kit->ki_range)
		return(NULL);

	/* shorthand var */
	kr = kit->ki_range;

	/* There is a boundary condition.
	 * The keys returned are directly out of the keys cache in the range.
	 * They are returned as just ptrs to the keys in the cache and not
	 * copies of the key. This is to reduce data allocations and copies. 
	 * This is OK as the cache lasts until the last key is reached and 
	 * ki_iterdone is called. Then it is freed in preparation to refill. 
	 * the keys cache. However, C 'for' loops will call ki_iternext and 
	 * then ki_iterdone before handing control to the users code. So on 
	 * the last key in the cache it will be freed before the user can use 
	 * it. So for this boundary case the last key is dup-ed and the copy 
	 * returned back. 
	 * Logic: If ki_curr equals the last key we copy it, if ki_curr equals 
	 * 0 we free it. The destory code will also free it if set.
	 */
	if ((kit->ki_curr == 0) && kit->ki_boundary) {
		/* first key of a new batch, boundary key not needed */
		ki_keydestroy(kit->ki_boundary, 1);
		kit->ki_boundary = NULL;
	}

	if (kit->ki_curr == kr->kr_keyscnt - 1) {
		/* Last key - handle boundary condition */
		kit->ki_boundary = ki_keydupf(&kr->kr_keys[kit->ki_curr], 1);
		if (!kit->ki_boundary) {
			return(NULL);
		}
		if (kit->ki_count > 0) kit->ki_count--;
		return(kit->ki_boundary);
	}

	if (kit->ki_curr < kr->kr_keyscnt) {
		/* Still keys in the cache, just return the key */
		if (kit->ki_count > 0) kit->ki_count--;
		return(&kr->kr_keys[kit->ki_curr]);
	} else {
		/* No more keys, ki_iterdone will catch it and return true */
		return(NULL);
	}
}
