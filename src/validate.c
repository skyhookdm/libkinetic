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

/**
 * ki_validate_kv(kv_t *kv, int verck, limit_t *lim)
 *
 *  kv		Always contains a key, but optionally may have a value
 *		However there must always value kiovec array of at least
 * 		element and should always be initialized. if unused,
 *		as in a get, kv_val[0].kiov_base=NULL and kv_val[0].kiov_len=0
 *  verck	ver is an optional field kv_t, but sometimes it is required.
 * 		When verck is set the version field must be set.
 *  lim 	Contains server limits
 *
 * Validate that the user is passing a valid kv structure
 *
 */
int
ki_validate_kv(kv_t *kv, int verck, klimits_t *lim)
{

	// Check the required key
	if (!kv || !kv->kv_key || kv->kv_keycnt < 1) { return (-1); }

	// Make sure it is a valid KTB
	if (!ki_valid(kv)) { return (-1); }
	
	// Total up the length across all vectors
	size_t total_keylen = 0;
	size_t key_fragndx = 0;
	for (key_fragndx = 0; key_fragndx < kv->kv_keycnt; key_fragndx++) {
		total_keylen += kv->kv_key[key_fragndx].kiov_len;
	}

	if (total_keylen > lim->kl_keylen) { return (-1); }

	/* Check the value vectors */
	if (!kv->kv_val || kv->kv_valcnt < 1) {
		debug_fprintf(stderr,
			      "Key *Value* is none or has 0 fragments\n");
		return (-1);
	}

	/* Total up the length across all vectors */
	size_t total_vallen = 0;
	size_t val_fragndx = 0;
	for (val_fragndx = 0; val_fragndx < kv->kv_valcnt; val_fragndx++) {
		total_vallen += kv->kv_val[val_fragndx].kiov_len;
	}

	if (total_vallen > lim->kl_vallen) { return (-1); }

	/* Check the versions -- minimum */
	if (kv->kv_ver &&
		((kv->kv_verlen < 1 || kv->kv_verlen > lim->kl_verlen))) {
		return (-1);
	}

	if (kv->kv_newver &&
		((kv->kv_newverlen < 1 || kv->kv_newverlen > lim->kl_verlen))) {
		return (-1);
	}

	/* If verck, then ver must be set, new version is optional */
	if (verck && !kv->kv_ver) { return(-1); }

	/* Check the data integrity chksum fields */
	if (kv->kv_disum) {
		/* if there is a sum, make there is a length */
		if ((kv->kv_disumlen < 1) ||
		    (kv->kv_disumlen > lim->kl_disumlen)) {
			return (-1);
		}
		
		/* Check the data integrity type */
		switch (kv->kv_ditype) {
		case KDI_SHA1:
		case KDI_SHA2:
		case KDI_SHA3:
		case KDI_CRC32C:
		case KDI_CRC64:
		case KDI_CRC32:
			break;
		default:
			return (-1);
		}
	}

	/* check the cache policy */
	// To avoid a warning about 0 not being defined in kditype_t enum
	if (kv->kv_cpolicy != (kcachepolicy_t) 0) {
		switch (kv->kv_cpolicy) {
			case KC_WT:
			case KC_WB:
			case KC_FLUSH:
				break;
			default:
				return (-1);
		}
	}

	return (0);
}

/**
 * ki_validate_range(krange_t *kr, limit_t *lim)
 *
 *  kr		Must be non-null. May contain a kr_start, kr_end and/or
 * 		kr_count. kr_flags should be zero'ed or with only KFR_ISTART
 *		and/or KFR_IEND set.
 *		If provided start and end keys total length must be
 *		greater than 0 and less than or equal to the max keylen
 *		Count must greater than 0 and less than or equal to max range
 * 		key count
 *  lim 	Contains server limits
 *
 * Validate that the user is passing a valid keyrange structure
 *
 */
int
ki_validate_range(krange_t *kr, klimits_t *lim)
{
	int i, len;

	/* Check for range structure */
	if (!kr)
		return (-1);

	// Make sure it is a valid KTB
	if (!ki_valid(kr)) { return (-1); }

	/* check the start & end key */
	if (kr->kr_start) {
		for (len=0, i=0; i<kr->kr_startcnt; i++)
			len += kr->kr_start[i].kiov_len;

		if (!len || len > lim->kl_keylen)
			return (-1);
	}

	if (kr->kr_end) {
		for (len=0, i=0; i<kr->kr_endcnt; i++)
			len += kr->kr_end[i].kiov_len;

		if (!len || len > lim->kl_keylen)
			return (-1);
	}

	/* check the flags */
	if (kr->kr_flags & ~KRF_VALIDMASK) {
		/* illegal bits used */
		return(-1);
	}

	/* check the count */
	if ((kr->kr_count != KVR_COUNT_INF) &&
	    (!kr->kr_count || kr->kr_count > lim->kl_rangekeycnt)) {
		return (-1);
	}

	return (0);
}

/**
 * ki_validate_kb(kb_t *kb)
 *
 *  kb 	contains the user passed in kb_t
 *
 * Validate that the user is asking for valid information. The type array
 * in the glrq may only have valid log types and they may not be repeated.
 *
 */
int
ki_validate_kb(kb_t *kb, kmtype_t msg_type)
{
	/* Check for range structure */
	if (!kb)
		return (-1);

	// Make sure it is a valid KTB
	if (!ki_valid(kb)) { return (-1); }

	switch(msg_type) {
	case KMT_STARTBAT:
		break;

	case KMT_ENDBAT:
	case KMT_ABORTBAT:
	case KMT_PUT:
	case KMT_DEL:
		if (kb->kb_bid >= KFIRSTBID) /* Likely valid Batch ID */
			break;

		return(-1);

	default:
		return(-1);
	}

	return(0);
}


/**
 * ki_validate_getlog(kgetlog_t *glrq)
 *
 *  glrq 	contains the user passed in glog
 *
 * Validate that the user is asking for valid information. The type array
 * in the glrq may only have valid log types and they may not be repeated.
 *
 */
int
ki_validate_glog(kgetlog_t *glrq)
{
	int i;
	int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */

	/* Check the the requested types */
	if (!glrq || !glrq->kgl_type || !glrq->kgl_typecnt)
		return(-1);

	// Make sure it is a valid KTB
	if (!ki_valid(glrq)) { return (-1); }

	/*
	 * PAK: what if LOG and MESSAGES are set? does log use messages
	 *      for its data?
	 */
	/* track how many times a type is in the array */
	util = temp = cap = conf = stat = mesg = lim = log = 0;
	for (i=0; i<glrq->kgl_typecnt; i++) {
		switch (glrq->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
			util++; break;
		case KGLT_TEMPERATURES:
			temp++; break;
		case KGLT_CAPACITIES:
			cap++; break;
		case KGLT_CONFIGURATION:
			conf++; break;
		case KGLT_STATISTICS:
			stat++; break;
		case KGLT_MESSAGES:
			mesg++; break;
		case KGLT_LIMITS:
			lim++; break;
		case KGLT_LOG:
			log++; break;
		default:
			return(-1);
		}
	}

	/* if a type is repeated, fail */
	if (util>1 || temp>1 || cap>1 || conf>1 ||
	    stat>1 || mesg>1 || lim>1 || log>1) {
			return(-1);
	}

	/* If log expect a logname, if not then there shouldn't be a name */
	if (log) {
		if (glrq->kgl_log.kdl_name) {
			/* do a range check on the length of the name */
			if (!glrq->kgl_log.kdl_len ||
			    (glrq->kgl_log.kdl_len > 1024)) {
				return(-1);
			}
		} else {
			/* Log requested but no name fail with default err */
			return(-1);
		}
	} else if (glrq->kgl_log.kdl_name) {
		/* Shouldn't have a name without a LOG type */
		return (-1);
	} /* no log and no logname, thats good */

	/* make sure all other ptrs and cnts are NULL and 0 */
	if (glrq->kgl_util	|| glrq->kgl_utilcnt	||
	    glrq->kgl_temp	|| glrq->kgl_tempcnt	||
	    glrq->kgl_stat	|| glrq->kgl_statcnt	||
	    glrq->kgl_msgs	|| glrq->kgl_msgslen) {
		return(-1);
	}

	errno = 0;
	return(0);
}

/**
 * ki_validate_getlog2(kgetlog_t *glrq, *glrsp)
 *
 *  glrq 	contains the original user passed in glog
 *  glrsp	contains the server returned glog
 *
 * Validate that the server answered the request and the glog structure is
 * correct.
 */
int
ki_validate_glog2(kgetlog_t *glrq, kgetlog_t *glrsp)
{
	int i, j;
	int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */

	/*
	 * Check the reqs and resp type exist types and
	 * that cnts should be the same
	 */
	if (!glrq || glrsp  ||
	    !glrq->kgl_type || !glrsp->kgl_type ||
	    (glrq->kgl_typecnt != glrsp->kgl_typecnt)) {
		return(-1);
	}

	/*
	 * build up vars that represent requested types, this will
	 * allow correct accounting by decrementing them as we find
	 * them in the responses below. The req was hopefully already
	 * validated before receiving a response this validation garantees
	 * unique requested types.
	 */
	util = temp = cap = conf = stat = mesg = lim = log = 0;
	for (i=0; i<glrq->kgl_typecnt; i++) {
		switch (glrq->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
			util++; break;
		case KGLT_TEMPERATURES:
			temp++; break;
		case KGLT_CAPACITIES:
			cap++; break;
		case KGLT_CONFIGURATION:
			conf++; break;
		case KGLT_STATISTICS:
			stat++; break;
		case KGLT_MESSAGES:
			mesg++; break;
		case KGLT_LIMITS:
			lim++; break;
		case KGLT_LOG:
			log++; break;
		default:
			return(-1);
		}
	}

	for (i=0; i<glrsp->kgl_typecnt; i++) {
		/* match this response type to a req type */
		for (j=0; j<glrq->kgl_typecnt; j++) {
			if (glrsp->kgl_type[i] == glrq->kgl_type[i]) {
				break;
			}
		}

		/*
		 * if the 'for' above is exhausted then no match,
		 * the resp has an answer that was not requested
		 */
		if (j == glrq->kgl_typecnt) {
			return(-1);
		}

		/* got a match */
		switch (glrsp->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
			util--;  /* dec to account for the req */

			/* if an util array is provided */
			if (!glrsp->kgl_util || !glrsp->kgl_utilcnt)
				return(-1);
			break;
		case KGLT_TEMPERATURES:
			temp--;  /* dec to account for the req */

			/* if an temp array is provided */
			if (!glrsp->kgl_temp || !glrsp->kgl_tempcnt)
				return(-1);
			break;
		case KGLT_CAPACITIES:
			cap++;  /* dec to account for the req */

			/* cap built into get log, no way to validate */
			break;
		case KGLT_CONFIGURATION:
			conf--;  /* dec to account for the req */

			/* conf built into get log, no way to validate */
			break;
		case KGLT_STATISTICS:
			stat--;  /* dec to account for the req */

			/* if an stat array is provided */
			if (!glrsp->kgl_stat || !glrsp->kgl_statcnt)
				return(-1);
			break;
		case KGLT_MESSAGES:
			mesg--;  /* dec to account for the req */

			/* if an msgs buf is provided */
			if (!glrsp->kgl_msgs || !glrsp->kgl_msgslen)
				return(-1);
			break;
		case KGLT_LIMITS:
			lim--;  /* dec to account for the req */

			/* limits built into get log, no way to validate */
			break;
		case KGLT_LOG:
			log--;  /* dec to account for the req */
			/* if an msgs buf is provided */
			if (!glrsp->kgl_msgs || !glrsp->kgl_msgslen)
				return(-1);
			break;
		default:
			/* Bad type */
			return(-1);
		}
	}

	/* if every req type was found in the resp all these should be 0 */
	if (util || temp || cap || conf || stat || mesg || lim || log) {
			return(-1);
	}

	errno = 0;
	return(0);
}


/**
 * ki_validate_kstats(kstats_t *kst)
 *
 *  kst		Nothing to validate other it is a create kinetic typed buffer
 *
 */
int
ki_validate_kstats(kstats_t *kst)
{

	// Check the required key
	if (!kst || !ki_valid(kst)) {
		return (-1);
	}

	errno = 0;
	return(0);
}
