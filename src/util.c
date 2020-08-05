/**
 * Copyright 2013-2020 Seagate Technology LLC.
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
 * ki_validate_kv(kv_t *kv, int force, limit_t *lim)
 *
 *  kv		Always contains a key, but optionally may have a value
 *		However there must always value kiovec array of at least 
 * 		element and should always be initialized. if unused,
 *		as in a get, kv_val[0].kiov_base=NULL and kv_val[0].kiov_len=0
 *  force	force is a flag used put's and del's, when set version
 * 		fields are ignored and therefore validating those fields 
 * 		can be weaker. When cleared the version field must be set. 
 *  lim 	Contains server limits	
 *
 * Validate that the user is passing a valid kv structure
 *
 */
int
ki_validate_kv(kv_t *kv, int force, klimits_t *lim)
{
	int i;
	size_t len;
	//int util, temp, cap, conf, stat, mesg, lim, log;

	errno = K_EINVAL;  /* assume we will find a problem */
	
	/* Check the required key */
	if (!kv || !kv->kv_key || kv->kv_keycnt < 1) 
		return(-1);

	/* Total up the length across all vectors */
	for (len=0, i=0; i<kv->kv_keycnt; i++)
		len += kv->kv_key[i].kiov_len;

	if (len > lim->kl_keylen)
		return(-1);

	/* Check the value vectors */	
	if (!kv->kv_val || kv->kv_valcnt < 1) 
		return(-1);

	/* Total up the length across all vectors */
	for (len=0, i=0; i<kv->kv_valcnt; i++)
		len += kv->kv_val[i].kiov_len;

	if (len > lim->kl_vallen)
		return(-1);
	
	/* Check the versions -- minimum */
	if (kv->kv_ver &&
	    ((kv->kv_verlen < 1) || (kv->kv_verlen > lim->kl_verlen)))
		return(-1);

	if (kv->kv_newver &&
	    ((kv->kv_newverlen < 1) || (kv->kv_newverlen > lim->kl_verlen)))
		return(-1);

	/* Require the version if force is cleared, new version is optional */
	if (!force && !kv->kv_ver) 
			return(-1);

	/* Check the data integrity chksum */
	if (kv->kv_disum &&
	    ((kv->kv_disumlen < 1) || (kv->kv_disumlen > lim->kl_disumlen)))
		return(-1);

	/* Check the data integrity type */
	switch (kv->kv_ditype) {
	case 0: /* zero is unused by the protocol but is valid for gets */
	case KDI_SHA1:
	case KDI_SHA2:
	case KDI_SHA3:
	case KDI_CRC32C:
	case KDI_CRC64:
	case KDI_CRC32:
		break;
	default:
		return(-1);
	}
	
	/* check the cache policy */
	switch (kv->kv_cpolicy) {
	case 0: /* zero is unused by the protocol but is valid for gets */
	case KC_WT:
	case KC_WB:
	case KC_FLUSH:
		break;
	default:
		return(-1);
	}
	
	errno = 0;
	return(0);
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
	
	/* assume we will find a problem */
	errno = K_EINVAL;

	/* Check for range structure */
	if (!kr)
		return (-1);

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
	if (!kr->kr_count || kr->kr_count > lim->kl_rangekeycnt)
		return (-1);

	errno = 0;
	return (0);
}

/**
 * ki_mk_firstkey(struct kiovec **key, size_t *keycnt, klimits_t *lim)
 *
 * Generate a structure containing the first permissible key 
 * when keys are sorted lexicographically by their byte representation.
 * This will a single zero byte.
 * 
 *  key		returned key vector array
 *  keycnt	returned number of elements in the key vector array
 *  lim		Session limits structure, unused in this function
 */
int
ki_mk_firstkey(struct kiovec **key, size_t *keycnt, klimits_t *lim)
{
	/* 
	 * Allocated these separately so that free can be called on
	 * both the key array and the key buffer, like other keys.
	 */
	*keycnt = 1;
	*key = KI_MALLOC(sizeof(struct kiovec));
	if (!*key)
		return(-1);
	
	(*key)[0].kiov_base = KI_MALLOC(1);
	if (!(*key)[0].kiov_base)
		return(-1);

	*((uint8_t *)(*key)[0].kiov_base) = 0x00;
	(*key)[0].kiov_len = 1;
	return(0);

}

/**
 * ki_mk_lastkey(struct kiovec **key, size_t *keycnt, klimits_t *lim)
 *
 * Generate a structure containing the last permissible key 
 * when keys are sorted lexicographically by their byte representation.
 * This will be an array of 0xFF bytes with a length kl_keylen.
 * 
 *  key		returned key vector array
 *  keycnt	returned number of elements in the key vector array
 *  lim		Session limits structure
 */
int
ki_mk_lastkey(struct kiovec **key, size_t *keycnt, klimits_t *lim)
{
	int i;
	uint8_t *buf;
	/* 
	 * Allocated these separately so that free can be called on
	 * both the key array and the key buffer, like other keys.
	 */
	*keycnt = 1;
	*key = KI_MALLOC(sizeof(struct kiovec));
	if (!*key)
		return(-1);

	buf = (uint8_t *)KI_MALLOC(lim->kl_keylen);
	if (!buf)
		return(-1);

	for(i=0; i<lim->kl_keylen; i++)
		buf[i] = 0xFF;

	(*key)[0].kiov_base = (void *)buf;
	(*key)[0].kiov_len = lim->kl_keylen;
	
	return(0);

}

/**
 * ki_mk_key(struct kiovec **key, size_t *keycnt, 
		void *buf, size_t len, klimits_t *lim)
 *
 * Generate a structure containing the last permissible key 
 * when keys are sorted lexicographically by their byte representation.
 * 
 *  key		returned key vector array
 *  keycnt	returned number of elements in the key vector array
 *  buf		byte string containing the key
 *  len		length of the buffer containing the ley
 *  lim		Session limits structure
 */
int
ki_mk_key(struct kiovec **key, size_t *keycnt,
	  void *buf, size_t len, klimits_t *lim)
{
	*keycnt = 1;
	*key = KI_MALLOC(sizeof(struct kiovec));
	if (!*key)
		return(-1);
	
	(*key)[0].kiov_base = buf;
	(*key)[0].kiov_len = len;
	return(0);
}

/** 
 * Return the session's klimits_t strucuture. 
 * Returning by value.
 */
klimits_t
ki_limits(int ktd)
{
	int rc;
	klimits_t elimits;
	struct ktli_config *cf;
	ksession_t *ses;

	memset((void *)&elimits, 0, sizeof(klimits_t));
	
	/* Get KTLI config */
	rc = ktli_config(ktd, &cf);
	if (rc < 0)
		return elimits;

	ses = (ksession_t *)cf->kcfg_pconf;
	
	return(ses->ks_l);
}
