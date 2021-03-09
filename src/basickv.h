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
#ifndef _BASICKV_H
#define _BASIC_KV_H
#include <stdint.h>

#include <kinetic/kinetic.h>

typedef struct bkv_open {
	char		*bkvo_host;
	char		*bkvo_port;
	uint64_t	bkvo_id;
	char		*bkvo_pass;
	int		bkvo_usetls;
} bkvs_open_t;

typedef struct bkv_limits {
	size_t	bkvl_klen;	/* Maximum key length */ 
	size_t	bkvl_vlen;	/* Maximum value length */
	size_t	bkvl_maxn;	/* Maximum n for putn, getn, deln */ 
} bkv_limits_t;

int bkv_open(bkvs_open_t *bkvo);
int bkv_close(int ktd);

int bkv_limits(int ktd,  bkv_limits_t *l);

int bkv_get(int ktd, void *key, size_t klen, void **value, size_t *vlen);
int bkv_put(int ktd, void *key, size_t klen, void  *value, size_t  vlen);
int bkv_del(int ktd, void *key, size_t klen);
int bkv_exists(int ktd, void *key, size_t klen);

/* Key spanning Gets, Puts and Dels */
int bkv_getn(int ktd, void *key, size_t klen, uint32_t n,
	     void **value, size_t *vlen);
int bkv_putn(int ktd, void *key, size_t klen, uint32_t *n,
	     void  *value, size_t  vlen);
int bkv_deln(int ktd, void *key, size_t klen, uint32_t n);
	

/* Maybe too comlex for basic kv
typedef void * opaque_t;
int bkv_getvers(int ktd, void *key, opaque_t *vers);
int bkv_putv(int ktd, void *key, opaque_t *vers, void  *value, size_t  vlen);
int bkv_cas(int ktd, void *key, 
		opaque_t overs, opaque_t *nvers, void *value, size_t vlen);
int bkv_cad(int ktd, void *key, opaque_t vers, void *value, size_t vlen);
*/


#endif /* _BASIC_KV_H */
