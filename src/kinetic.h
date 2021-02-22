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
#ifndef __KINETIC_INTERFACE_H
#define __KINETIC_INTERFACE_H

#include "kinetic_types.h"


// ------------------------------
// macros for printing/logging

// define log levels
#define LOGLEVEL_NONE  0
#define LOGLEVEL_INFO  1
#define LOGLEVEL_DEBUG 2

// this is the log level set for the program
#define LOGLEVEL LOGLEVEL_NONE

// macros that use the log level
#if LOGLEVEL >= LOGLEVEL_DEBUG
	#define debug_fprintf(...) fprintf(__VA_ARGS__)
#else
	#define debug_fprintf(...) {}
#endif

#if LOGLEVEL >= LOGLEVEL_INFO
	#define info_fprintf(...) fprintf(__VA_ARGS__)
#else
	#define info_fprintf(...) {}
#endif

// printf's as an alias to fprintf's for convenience
#define debug_printf(...) debug_fprintf(stdout, __VA_ARGS__)
#define info_printf(...)  info_fprintf(stdout, __VA_ARGS__)


// ------------------------------
// The API
int ki_open(char *host, char *port, uint32_t usetls, int64_t id, char *hmac);
int ki_close(int ktd);

// Kinetic Type Interfaces
void *ki_create(int ktd, ktype_t kt);
int   ki_clean(void *p);
int   ki_destroy(void *p);
int   ki_valid(void *p);

// Kinetic synchronous I/O interfaces
kstatus_t ki_put(int ktd, kbatch_t *kb, kv_t *kv);
kstatus_t ki_cas(int ktd, kbatch_t *kb, kv_t *kv);
kstatus_t ki_del(int ktd, kbatch_t *kb, kv_t *key);
kstatus_t ki_cad(int ktd, kbatch_t *kb, kv_t *key);

kstatus_t ki_get(int ktd, kv_t *key);
kstatus_t ki_getnext(int ktd, kv_t *key, kv_t *next);
kstatus_t ki_getprev(int ktd, kv_t *key, kv_t *prev);
kstatus_t ki_getversion(int ktd, kv_t *key);
kstatus_t ki_getrange(int ktd, krange_t *kr);
kstatus_t ki_getlog(int ktd, kgetlog_t *glog);

kstatus_t ki_abortbatch(int ktd, kbatch_t *kb);
kstatus_t ki_submitbatch(int ktd, kbatch_t *kb);

// Kinetic asynchronous I/O interfaces
kstatus_t ki_aio_put(int ktd, kbatch_t *kb, kv_t *kv,  void *cctx, kio_t **kio);
kstatus_t ki_aio_cas(int ktd, kbatch_t *kb, kv_t *kv,  void *cctx, kio_t **kio);
kstatus_t ki_aio_del(int ktd, kbatch_t *kb, kv_t *key, void *cctx, kio_t **kio);
kstatus_t ki_aio_cad(int ktd, kbatch_t *kb, kv_t *key, void *cctx, kio_t **kio);

kstatus_t ki_aio_get(int ktd, kv_t *key, void *cctx, kio_t **kio);
kstatus_t ki_aio_getnext(int ktd, kv_t *key, kv_t *next,
			 void *cctx, kio_t **kio);
kstatus_t ki_aio_getprev(int ktd, kv_t *key, kv_t *prev,
			 void *cctx, kio_t **kio);
kstatus_t ki_aio_getversion(int ktd, kv_t *key, void *cctx, kio_t **kio);

kstatus_t ki_aio_abortbatch(int ktd,  kbatch_t *kb, void *cctx, kio_t **kio);
kstatus_t ki_aio_submitbatch(int ktd, kbatch_t *kb, void *cctx, kio_t **kio);

kstatus_t ki_aio_complete(int ktd, kio_t *kio, void **cctx);

int ki_poll(int ktd, int timeout);

// ------------------------------
// key iterator functions
struct kiovec *ki_start(kiter_t *kit, krange_t *kr);
struct kiovec *ki_next(kiter_t *kit);


// ------------------------------
// utility functions

// for information structures
klimits_t      ki_limits(int ktd);
kstatus_t      ki_setclustervers(int ktd, int64_t vers);

// for key utilities/helpers

/* 
 * Create a new key vector from an existing buffer without copying it,
 * keycnt=1 for returned vector 
 */
struct kiovec *ki_keycreate(void *keybuf, size_t keylen);

/* 
 * Destroy the provided key, frees both the key buffers(if any) 
 * and key vector 
*/
void ki_keydestroy(struct kiovec *key, size_t keycnt);

/* 
 * Duplicate the key and vector structure, newkeycnt=keycnt for 
 * returned vector
 */
struct kiovec *ki_keydup(struct kiovec *key, size_t keycnt);

/*
 * Duplicate the key flattening into a single vector element, newkeycnt=1 for 
 * returned vector 
 */
struct kiovec *ki_keydupf(struct kiovec *key, size_t keycnt);

/*
 * Return a new key vector with the provided keybuf added as a 
 * new vector element:
 * 	o at location [0] for prepend
 * 	o at location [keycnt] for append
 * newkeycnt=keycnt++ for the returned vector. Original vector is invalid after 
 * this call.  
 */
struct kiovec *ki_keyprepend(struct kiovec *key, size_t keycnt,
			     void *keybuf, size_t keylen);
struct kiovec *ki_keyappend(struct kiovec *key, size_t keycnt,
			    void *keybuf, size_t keylen);

/*
 *  Return either a key that represents 
 * 	o the very first, key = "\x00"
 * 	o the last key for a given key size,  key = "\xFF...\xFF"
 */
struct kiovec *ki_keyfirst();
struct kiovec *ki_keylast(size_t len);

// for iterator management
krange_t *ki_rangecpy(krange_t *dst, krange_t *src);
krange_t *ki_rangedup(int ktd, krange_t *kr);

// for checksum computation
struct kbuffer compute_digest(struct kiovec *io_vec, size_t io_cnt,
			      const char *digest_name);

// for range 
int ki_rangefree(krange_t *kr);

const char *ki_error(kstatus_t ks);


#endif // __KINETIC_INTERFACE_H
