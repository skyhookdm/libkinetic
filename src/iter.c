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
 * The type kiter_t is opaque and managed completely by libkinetic.
 *
 * An iterator must be created (ki_create) before it can be used. Once
 * created it can be used many times before it is destroyed (ki_destroy).
 * Although a given iterator can be used many times, it can not be shared
 * and used simultaneously.
 *
 * An iterator is started by calling ki_start with a valid iterator (kiter_t)
 * and a valid range (krange_t) to iterate though. The provided range is
 * copied for internal use and is no longer needed by the iterator. As for the
 * range, the kinetic iterator honors start and end keys, but also
 * honors the abscence of these keys.  If these keys not provided, the first
 * legal key and last legal key are substituted. The Kinetic iterator also
 * honors the inclusive flags for both the start and end keys. Key counts
 * from 1 to infinity are also supported.  NOTE: Currently reverse is NOT
 * supported. Successive calls to ki_start reset the iterator to the
 * new range provided. ki_iterstart always returns the first key.
 *
 * ki_next incrments the iterator to the next key in the sequence.
 *
 * If ki_start or ki_next return a non-NUll value the iterator continues.
 *
 * An example would be:
 *	struct kiovec   *k;
 *	krange_t	kr;
 *	kiter_t		*kit;
 *
 *	kit = ki_create(ktd, KITER_T);
 *
 *	for (k = ki_start(kit, &kr); k; k = ki_next(kit)) {
 * 		// do something with k
 *	}
 *
 *	ki_destroy(kit);
 *
 * Since ki_getrange is server limited to retrieve < 1000 keys at a time,
 * this iterator uses a key range window to permit ranges that exceed that
 * limit. The window is initially filled in ki_start and then when that
 * window is depleted by subsequent calls to ki_next the next window in
 * the range is retieved.  Right the code will experience latency spikes
 * each time the window is depleted and a synchronous call to ki_getrange
 * is made.  The code has been written to support AIO when it is available.
 * there are 2 windows defined in kiter_t. This will allow the aio getrange
 * call to be made before the window is depleted and then when it is emptied
 * the AIO call can be completed. This will allow the round trip RPC to occur
 * in the background, hiding the latency.
 */

/**
 *  This is called by ki_create to init the kiter
 *  NOTE: perhaps the range should be passed here so that rreq can be
 *  reliably reused.
 */
int
i_iterinit(int ktd, kiter_t *ckit)
{
	int rc;
	ksession_t *ses;
	struct ktli_config *cf;
	struct kiter *kit = (struct kiter *) ckit;

	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) { return (rc); }

	ses	      = (ksession_t *) cf->kcfg_pconf;
	kit->ki_rreq  = ki_create(kit->ki_ktd, KRANGE_T);
	kit->ki_rwin1 = ki_create(kit->ki_ktd, KRANGE_T);
	kit->ki_rwin2 = ki_create(kit->ki_ktd, KRANGE_T);

	if (!kit->ki_rreq || !kit->ki_rwin1 || !kit->ki_rwin2) {
		/* Something failed clean it up. Destroy just returns on NULL */
		ki_destroy(kit->ki_rreq);
		kit->ki_rreq = NULL;

		ki_destroy(kit->ki_rwin1);
		kit->ki_rwin1 = NULL;

		ki_destroy(kit->ki_rwin2);
		kit->ki_rwin2 = NULL;

		return (-1);
	}

	kit->ki_ktd	  = ktd;
	kit->ki_curwin	  = NULL;
	kit->ki_curkey	  = 0;
	kit->ki_seenkeys  = 0;
	kit->ki_maxkeyreq = ses->ks_l.kl_rangekeycnt;
	kit->ki_kio	  = NULL;
	
	return (0);
}

/**
 *  This is called by ki_clean.
 *  This is called after ki_clean invokes the ktb_ctx destructor.
 */
void
i_rangeclean(krange_t *kr)
{
	ki_keydestroy(kr->kr_start, kr->kr_startcnt);
	ki_keydestroy(kr->kr_end  , kr->kr_endcnt  );

	memset(kr, 0, sizeof(krange_t));
}

/**
 *  This is called by ki_clean.
 *  This just delegates a clean call to each key range structure.
 */
void
i_iterclean(kiter_t *ckit)
{
	struct kiter *kit = (struct kiter *) ckit;

	ki_clean(kit->ki_rreq );
	ki_clean(kit->ki_rwin1);
	ki_clean(kit->ki_rwin2);
}

/**
 *  This is called by ki_destroy.
 */
void
i_iterdestroy(kiter_t *ckit)
{
	struct kiter *kit = (struct kiter *) ckit;

	ki_destroy(kit->ki_rreq);
	kit->ki_rreq = NULL;

	ki_destroy(kit->ki_rwin1);
	kit->ki_rwin1 = NULL;

	ki_destroy(kit->ki_rwin2);
	kit->ki_rwin2 = NULL;
}

/**
 * An iter is restartable with a new range.
 */
struct kiovec *
ki_start(kiter_t *ckit, krange_t *ckr)
{
	kstatus_t krc;
	struct kiter *kit = (struct kiter *) ckit;

	/* All of these should already exist */
	if (!kit          || !ckr            || !ckr->kr_count ||
	    !kit->ki_rreq || !kit->ki_rwin1  || !kit->ki_rwin2) {
		return (NULL);
	}

	/* Set the current window to window 1 */
	kit->ki_curwin = kit->ki_rwin1;

	/* Clean the ranges */
	// TODO: if we don't clean rreq each time for this iter, we can re-use it.
	i_rangeclean(kit->ki_rreq );
	i_rangeclean(kit->ki_rwin1);
	i_rangeclean(kit->ki_rwin2);

	/*
	 * Make a copy of the callers KR,
	 * hence forward not dependent on that KRs lifecycle
	 * This is the guiding reference range for the life of the iteration
	 */
	if (ki_rangecpy(kit->ki_rreq, ckr) == NULL) {
		return (NULL);
	}

	/* Setup the first getrange call to use the caller provided range */ 
	if (ki_rangecpy(kit->ki_curwin, kit->ki_rreq) == NULL) {
		return (NULL);
	}

	/* Set the range count appropriately */
	if ((kit->ki_rreq->kr_count == KVR_COUNT_INF) ||
	    (kit->ki_rreq->kr_count > kit->ki_maxkeyreq)) {
		kit->ki_curwin->kr_count = kit->ki_maxkeyreq;
	}

	/* Fill the window */
	krc = ki_getrange(kit->ki_ktd, kit->ki_curwin);
	if ((krc != K_OK) || (!kit->ki_curwin->kr_count)) {
		return (NULL);
	}

	kit->ki_curkey   = 0;
	kit->ki_seenkeys = 1;
	return (&kit->ki_curwin->kr_keys[kit->ki_curkey]);
}

/**
 * Increment an iter to the next key
 */
struct kiovec *
ki_next(kiter_t *ckit)
{
	uint32_t       keysleft;
	kstatus_t      krc;
	krange_t      *curwin;
	struct kiovec *lastkey;
	struct kiter  *kit = (struct kiter *) ckit;

	if (!kit || !kit->ki_rreq || !kit->ki_rwin1 || !kit->ki_rwin1)
		return (NULL);
	
	/* Check the keys seen against the requested count */
	if ((kit->ki_rreq->kr_count != KVR_COUNT_INF         ) &&
	    (kit->ki_seenkeys       >= kit->ki_rreq->kr_count)) {
		/* The range count has been exhausted, return */
		return (NULL);
	}

	/* 
	 * NOTE: when ki_aio_getrange becomes available it is here
	 * that the the async call be made toward the end of the current
	 * window range. Then when the current window range is exhausted
	 * the getrange call can be completed.  This will smooth out
	 * the latency spike in the iter process by getting windows in
	 * background.
	 */

	/* to reduce code lengths */
	curwin = kit->ki_curwin;

	/* bump the curkey to look at next key */
	kit->ki_curkey++;

	/* Check the current window count */
	if (kit->ki_curkey >= curwin->kr_keyscnt) {
		/* Current window exhuasted, need a new window */

		/* Make a copy of the last key before freeing the keys */
		lastkey = ki_keydupf(&curwin->kr_keys[curwin->kr_keyscnt-1], 1);

		/* Clean up the start key and the keys buffer, keep the endkey */
		ki_keydestroy(curwin->kr_start, curwin->kr_startcnt);

		// NOTE: (#50) we do **not** want to keydestroy pointers to protobuf data
		// ki_keydestroy(curwin->kr_keys, curwin->kr_keyscnt);

		/* Use the last key for the start window range */
		curwin->kr_start    = lastkey;
		curwin->kr_startcnt = 1;
		curwin->kr_keys     = NULL;
		curwin->kr_keyscnt  = 0;

		/* Clear the inclusive start field to not repeat keys */
		KR_FLAG_CLR(curwin, KRF_ISTART);

		/*
		 * Set the range count appropriately, remember kr_count
		 * could be -1 (KVR_COUNT_INF), in that case keysleft will
		 * be a neg number but the first clause should catch that
		 * case and not use keysleft.
		 */
		keysleft = kit->ki_rreq->kr_count - kit->ki_seenkeys;
		if ((kit->ki_rreq->kr_count == KVR_COUNT_INF) ||
		    (keysleft > kit->ki_maxkeyreq)) {
			kit->ki_curwin->kr_count = kit->ki_maxkeyreq;
		}
		else {
			kit->ki_curwin->kr_count = keysleft;
		}

		/* Fill the window */
		krc = ki_getrange(kit->ki_ktd, curwin);
		if ((krc !=  K_OK) || (!curwin->kr_count)) {
			return (NULL);
		}
		kit->ki_curkey = 0;
	}

	kit->ki_seenkeys++;
	return (&kit->ki_curwin->kr_keys[kit->ki_curkey]);
}
