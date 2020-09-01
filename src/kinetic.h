/**
 * Copyright 2013-2015 Seagate Technology LLC.
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

klimits_t      ki_limits(int ktd);
kstatus_t      ki_setclustervers(int ktd, int64_t vers);

kstatus_t      ki_put(int ktd, kbatch_t *kb, kv_t *kv);
kstatus_t      ki_cas(int ktd, kbatch_t *kb, kv_t *kv);
kstatus_t      ki_del(int ktd, kbatch_t *kb, kv_t *key);
kstatus_t      ki_cad(int ktd, kbatch_t *kb, kv_t *key);

kbatch_t      *ki_batchcreate(int ktd);
kstatus_t      ki_batchend(int ktd, kbatch_t *kb);

kstatus_t      ki_get(int ktd, kv_t *key);
kstatus_t      ki_getnext(int ktd, kv_t *key, kv_t *next);
kstatus_t      ki_getprev(int ktd, kv_t *key, kv_t *prev);
kstatus_t      ki_getversion(int ktd, kv_t *key);
kstatus_t      ki_range(int ktd, krange_t *kr);
kstatus_t      ki_getlog(int ktd, kgetlog_t *glog);

kiter_t       *ki_itercreate(int ktd);
int            ki_iterfree(kiter_t *kit);
int            ki_iterdone(kiter_t *kit);
struct kiovec *ki_iterstart(kiter_t *kit, krange_t *kr);
struct kiovec *ki_iternext(kiter_t *kit);

struct kiovec *ki_keycreate(void *keybuf, size_t keylen);
struct kiovec *ki_keydup(struct kiovec *key, size_t keycnt);
struct kiovec *ki_keydupf(struct kiovec *key, size_t keycnt);
struct kiovec *ki_keyprefix(struct kiovec *key, size_t keycnt, void *keybuf, size_t keylen);
struct kiovec *ki_keypostfix(struct kiovec *key, size_t keycnt, void *keybuf, size_t keylen);
struct kiovec *ki_keyfirst();
struct kiovec *ki_keylast(size_t len);

int ki_keyfree(struct kiovec *key, size_t keycnt);

krange_t *ki_rangedup(krange_t *kr);
int       ki_rangefree(krange_t *kr);

struct kbuffer compute_digest(struct kiovec *io_vec, size_t io_cnt, const char *digest_name);


#endif // __KINETIC_INTERFACE_H
