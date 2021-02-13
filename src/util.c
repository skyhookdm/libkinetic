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

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"

/**
 * Utility functions for the kinetic library
 */

/*
 * Completely arbitrary value. Should be greater than any known Kinetic
 * keylen implementation.  Used to check reasonable key lengths prior to
 * allocations without needing kinetic server limits. Prevents bad or
 * malicious code from allocating HUGE keys. Largest keylen to date is 1024.
 */
#define MAXKEYLEN 4096

/**
 * create a key vector with a single element around an existing buffer
 * Assumes caller sets keycnt to 1
 */
struct kiovec *
ki_keycreate(void *keybuf, size_t keylen)
{
	struct kiovec *key;

	if (!keybuf || keylen > MAXKEYLEN)
		return(NULL);

	/* Create the kiovec array */
	key = (struct kiovec *)KI_MALLOC(sizeof(struct kiovec));
	if (!key) {
		return(NULL);
	}

	/* Hang the user provided buffer */
	key[0].kiov_base = keybuf;
	key[0].kiov_len = keylen;
	return(key);
}

/**
 * ki_keydestroy
 *  Free the given key
 */
void
ki_keydestroy(struct kiovec *key, size_t keycnt)
{
	int i;

	if (!key)
		return;

	for(i=0;i<keycnt;i++) {
		if (key[i].kiov_base) {
			KI_FREE(key[i].kiov_base);
			key[i].kiov_base = 0;
			key[i].kiov_len = 0;
		}
	}

	KI_FREE(key);

	return;
}

/**
 * reallocates a key vector to include a prefix buffer
 * Assumes caller sets keycnt to keycnt + 1
 */
struct kiovec *
ki_keyprepend(struct kiovec *key, size_t keycnt, void *keybuf, size_t keylen)
{
	int i;
	size_t newcnt, newlen;
	struct kiovec *new;

	if (!key || !keybuf || keylen > MAXKEYLEN)
		return(NULL);

	/* Realloc the kiovec array */
	newcnt = keycnt + 1;
	newlen = sizeof(struct kiovec) * newcnt;
	new = (struct kiovec *)KI_REALLOC(key, newlen);
	if (!key) {
		return(NULL);
	}

	/* Move everything down - start at the last index and work up */
	for(i=newcnt-1; i>0; i--)
		memcpy(&new[i], &new[i-1],  sizeof(struct kiovec));

	/* Hang the user provided prefix buffer as the first index */
	new[0].kiov_base = keybuf;
	new[0].kiov_len  = keylen;
	return(new);

}

/**
 * reallocates a key vector to include a postfix buffer
 * Assumes caller sets keycnt to keycnt + 1
 */
struct kiovec *
ki_keyappend(struct kiovec *key, size_t keycnt, void *keybuf, size_t keylen)
{
	size_t newcnt, newlen;
	struct kiovec *new;

	if (!key || !keybuf || keylen > MAXKEYLEN)
		return(NULL);

	/* Realloc the kiovec array */
	newcnt = keycnt + 1;
	newlen = sizeof(struct kiovec) * newcnt;
	new = (struct kiovec *)KI_REALLOC(key, newlen);
	if (!key) {
		return(NULL);
	}

	/* No need to move anything as realloc tacks the new space at the end */

	/* Hang the user provided prefix buffer as the last index */
	new[newcnt-1].kiov_base = keybuf;
	new[newcnt-1].kiov_len  = keylen;
	return(new);

}

/**
 * ki_keydup
 *
 * Duplicate the given key
 */
struct kiovec *
ki_keydup(struct kiovec *key, size_t keycnt)
{
	int i;
	struct kiovec *new;

	if (!keycnt)
		return(NULL);

	/* Create and clear the kiovec array */
	new = (struct kiovec *)KI_MALLOC(sizeof(struct kiovec) * keycnt);
	if (!new) {
		return(NULL);
	}
	memset(new, 0, sizeof(struct kiovec) * keycnt);

	/* Copy the key */
	for(i = 0; i < keycnt; i++) {
		new[i].kiov_len  = key[i].kiov_len;
		new[i].kiov_base = KI_MALLOC(key[i].kiov_len);

		if (!new[i].kiov_base) {
			ki_keydestroy(new, i-1);
			KI_FREE(new);
			return(NULL);
		}

		memcpy(new[i].kiov_base, key[i].kiov_base, key[i].kiov_len);
	}

	return(new);
}

/**
 * ki_keydupf()
 *
 * Duplicate the given key but flatten the vector to a single element
 * The resulting key should have a keycnt = 1
 */
struct kiovec *
ki_keydupf(struct kiovec *key, size_t cnt)
{
	int i, klen;
	void *kbuf;
	struct kiovec *new;

	if (!cnt)
		return(NULL);

	new = (struct kiovec *)KI_MALLOC(sizeof(struct kiovec));
	if (!new) {
		return(NULL);
	}

	/* Count total key length */
	klen = 0;
	for(i=0;i<cnt;i++) {
		klen +=  key[i].kiov_len;
	}

	/* allocate the single key buffer */
	kbuf = KI_MALLOC(klen);
	if (!kbuf) {
		KI_FREE(new);
		return(NULL);
	}

	/* Hang the new buffer on the new key vector. */
	new[0].kiov_base = kbuf;
	new[0].kiov_len  = klen;

	/* copy the existing key buffers into the single kbuf */
	for(i=0;i<cnt;i++) {
		memcpy(kbuf, key[i].kiov_base, key[i].kiov_len);
		kbuf += key[i].kiov_len;
	}

	return(new);
}

/**
 * ki_keyfirst()
 *
 * Generate a structure containing the first permissible key
 * when keys are sorted lexicographically by their byte representation.
 * This will a single zero byte. This will always yield a keycnt = 1
  */
struct kiovec *
ki_keyfirst()
{
	struct kiovec *key;
	/*
	 * Allocated these separately so that free can be called on
	 * both the key array and the key buffer, like other keys.
	 */
	key = KI_MALLOC(sizeof(struct kiovec));
	if (!key)
		return(NULL);

	key[0].kiov_base = KI_MALLOC(1);
	if (!key[0].kiov_base) {
		KI_FREE(key);
		return(NULL);
	}

	*(uint8_t *)(key[0].kiov_base) = 0x00;
	key[0].kiov_len = 1;
	return(key);
}

/**
 * ki_keylast(size_t len)
 *
 * Generate a structure containing the last permissible key for a given
 * key len when keys are sorted lexicographically by their byte
 * representation. Usually limits->kl_keylen will be passed in.
 * This will be an array of 0xFF bytes with a length of len.
 * This will always yield a keycnt = 1
 *
 *  len		 length of key to create
 */
struct kiovec *
ki_keylast(size_t len)
{
	int i;
	uint8_t *buf;
	struct kiovec *key;
	/*
	 * Allocated these separately so that free can be called on
	 * both the key array and the key buffer, like other keys.
	 */
	key = KI_MALLOC(sizeof(struct kiovec));
	if (!key)
		return(NULL);

	buf = (uint8_t *)KI_MALLOC(len);
	if (!buf) {
		KI_FREE(key);
		return(NULL);
	}

	for(i=0; i<len; i++)
		buf[i] = 0xFF;

	key[0].kiov_base = (void *)buf;
	key[0].kiov_len = len;

	return(key);
}



/**
 * ki_rangedup
 *
 * Duplicate the given range
 */
krange_t *
ki_rangedup(int ktd, krange_t *kr)
{
	krange_t *new;

	if (!kr)
		return(NULL);

	new = ki_create(ktd, KRANGE_T);

	/* duplicate the passed in krange_t */
	memcpy(new, kr, sizeof(krange_t));

	if (kr->kr_keys) {
		new->kr_keyscnt = kr->kr_keyscnt; /* Don't flatten */
		new->kr_keys = ki_keydup(kr->kr_keys, kr->kr_keyscnt);
		if (!new->kr_keys) {
			ki_destroy(new);
			return(NULL);
		}

	}

	if (kr->kr_start) {
		new->kr_startcnt = 1; /* dup flatten below */
		new->kr_start = ki_keydupf(kr->kr_start, kr->kr_startcnt);
		if (!new->kr_start) {
			ki_keydestroy(new->kr_keys, new->kr_keyscnt);
			ki_destroy(new);
			return(NULL);
		}
	}

	if (kr->kr_end) {
		new->kr_endcnt = 1; /* dup flatten below */
		new->kr_end = ki_keydupf(kr->kr_end, kr->kr_endcnt);
		if (!kr->kr_end) {
			ki_keydestroy(new->kr_start, new->kr_startcnt);
			ki_keydestroy(new->kr_keys,  new->kr_endcnt);
			ki_destroy(new);
			return(NULL);
		}
	}

	return(new);
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

/**
 * compute_digest defaults to sha1 for the data integrity algorithm. If provided, then
 * `digest_name` will be used. For supported digestnames, reference:
 * https://github.com/openssl/openssl/blob/master/crypto/objects/objects.txt
 */
struct kbuffer compute_digest(struct kiovec *io_vec, size_t io_cnt, const char *digest_name) {
    const EVP_MD *digestfn_info;
    if (!digest_name) { digestfn_info = EVP_get_digestbyname("sha1");      }
    else              { digestfn_info = EVP_get_digestbyname(digest_name); }

    unsigned int   final_digestlen;
    unsigned char *digest_result = (unsigned char *) malloc(sizeof(char) * EVP_MAX_MD_SIZE);
    if (!digest_result) { return (struct kbuffer) { .base = NULL, .len = 0 }; }

    // initialize context for calculating the digest message
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();

    EVP_DigestInit_ex(mdctx, digestfn_info, NULL);

    // accumulate the digest message into mdctx
    for (size_t io_ndx = 0; io_ndx < io_cnt; io_ndx++) {
        EVP_DigestUpdate(mdctx, io_vec[io_ndx].kiov_base, io_vec[io_ndx].kiov_len);
    }

    // finalize the digest message into digest_result
    EVP_DigestFinal_ex(mdctx, digest_result, &final_digestlen);

    // cleanup the context
    EVP_MD_CTX_free(mdctx);

    return (struct kbuffer) {
        .len  = final_digestlen,
        .base = digest_result,
    };
}
