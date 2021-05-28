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
#include "protocol_interface.h"

// This is a 3rd-party dep; but included by ktli.h anyways
#include "list.h"


int  i_iterinit(int ktd, kiter_t *kit);
void i_iterdestroy(kiter_t *kit);
extern char *ki_ktype_label[];

/*
 * This module implements the Kinetic Typed Buffer (KTB) infrastructure.
 * While KTB is an interal structure, there are a handful of access
 * functions that will be used by callers. All instances of Kinetic data
 * types used in the Kinetic API signatures must created and destroyed
 * by KTB's exported functions. This includes both opaque and non-opaque
 * data structures, the full list is in ktype_t.  For example, a key
 * value data structure(kv_t), needed for a ki_get(), is required to be
 * created by this module and ultimately destroyed by this module.  The
 * need for this was driven by a zero or near zero copy policy instituted
 * in this Kinetic API. To avoid copying data into user provided structures
 * means that internal allocated buffers containing keys, kv meta data,
 * log data etc, are passed directly back to the caller. The caller has
 * no way of knowing how to deallocate these buffers, as they may be
 * regions of a much karger freeable buffer. By formalizing these Kinetic
 * data structures, requiring the callers to create and destroy them,
 * permits the library the opportunity to hide and perform the details
 * of cleaning up these internal buffers. The callers are still allowed
 * to hang their own buffers on these structures once created, but the
 * API will not accept base structures not created via this module.
 * Callers are still responsible for buffers they allocate and hang on
 * these managed structures. This creates a sound rule that what the
 * API allocates, the API is responsible for deallocating and conversely
 * what the caller allocates, the caller is responsible for deallocating.
 * There is one exception to this rule, values.  Value buffers, either
 * allocated by the caller or the library, must be deallocated by the caller.
 * Values received from the API are returned as a single freeable ptr and
 * must be deallocated by the caller.
 *
 * The KTB infrastructure is hidden from the caller and should never be
 * exploited by the caller.
 */

/* Yields the 8 byte string "KTBUF\0\0\0" */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define KTB_MAGIC   0x000000465542544b
#else
#define KTB_MAGIC   0x4b54425546000000
#endif

/*
 * Kinetic Typed Buffer data structure.
 * This is the variable sized buffer that contains the metadata as well as
 * client requested data structure. Note that ktb_buf[] is an ISO C99
 * flexible array member and is the ptr to the user requested structure.
 * All operations after ki_create use that returned ptr as the primary param.
 * The base ktb ptr is calculated using this caller provided ptr. The
 * ktb is fully validated by examining the magic number and the type.
 * This is done before any operation is attempted.
 */
typedef struct ktb {
	/* These elements are the management info for the buffer. */
	uint64_t   ktb_magic; 			/* ktb validation */
	ktype_t    ktb_type;  			/* ktb_buf ptr type */
	uint32_t   ktb_len;   			/* Full ktb length */
	uint32_t   ktb_ktd;			/* Connection desciptor */
	
	/* KT Buffer management routines. */
	void      *ktb_ctx;			/* Context for destroy */
	void     (*ktb_destroy)(void *ctx);	/* Type specific destroy */

	uint8_t    ktb_buf[];  			/* Must be Last */
} ktb_t;

/* Given a ptr, returns a ptr ktbkinetic typed buffer that wraps it. */
#define ktb_base(_p)	((ktb_t *)((char *)_p - sizeof(ktb_t)))

/* Given a ptr, is it encased in avalid kinetic typed buffer */
#define ktb_isvalid(_p)	((_p) &&					\
			 (ktb_base(_p)->ktb_magic == KTB_MAGIC) &&	\
			 (ktb_base(_p)->ktb_type  >  KT_NONE)   &&	\
			 (ktb_base(_p)->ktb_type  <  KT_LAST))

/*
 * Helper function for determing the variable length part of the ktb
 */
static uint32_t
ktb_buf_len(ktype_t t)
{
	switch(t) {

	case KV_T:
		return((uint32_t)sizeof(kv_t));

	case KRANGE_T:
		return((uint32_t)sizeof(krange_t));

	case KITER_T:
		/* opaque type, use the backing type */
		return((uint32_t)sizeof(ki_t));

	case KBATCH_T:
		/* opaque type, use the backing type */
		return((uint32_t)sizeof(kb_t));

	case KGETLOG_T:
		return((uint32_t)sizeof(kgetlog_t));

	case KVERSION_T:
		return((uint32_t)sizeof(kversion_t));

	case KSTATS_T:
		return((uint32_t)sizeof(kstats_t));

	default:
		return(0);
	}
}

/* 
 * Create the requested data structure.
 * In addition to the type requested, a current ktd is required. The ktd is
 * needed for the creation of certain types such as kbtach and kiter. It can 
 * also be used for debug accounting internally (although not yet implemented).
 */
void *
ki_create(int ktd, ktype_t t)
{
	ktb_t *k;
	void *p;
	uint32_t l = sizeof(ktb_t) + ktb_buf_len(t);
	
	if ((t <= KT_NONE) || (t >= KT_LAST)) {
		debug_printf("create: bad type\n");
		return NULL;
	}

	k =  KI_MALLOC(l);
	if (!k) {
		return(NULL);
	}
	
	memset(k, 0, l);
	k->ktb_magic = KTB_MAGIC;
	k->ktb_type  = t;
	k->ktb_len   = l;
	k->ktb_ktd   = ktd;

	p = (void *) k->ktb_buf;

	debug_printf("KI_CREATE : %p (%s)\n", p, ki_ktype_label[t]);

	/* additional setup is required */
	switch(t) {
	case KITER_T:
		i_iterinit(ktd, (kiter_t *)p); break;
	case KBATCH_T:
		b_startbatch(ktd, (kbatch_t *)p); break;
		break;
	default:
		break;
	}

	return(p);
}

ktb_t*
ki_ptr(void *p)
{
	if (!ktb_isvalid(p)) { return NULL; }

	return ktb_base(p);
}


kstatus_t
ki_addctx(void *p, void *ctx, void (*destructor)(void *ctx))
{
	ktb_t *k;
	LIST  *protobuf_list;

	debug_printf("ki_setctx: %p\n", p);
	if (!ktb_isvalid(p)) { return (K_EINVAL); }

	k = ktb_base(p);

	// set destructor for elements in the list
	k->ktb_destroy = destructor;

	// get list reference; allocate if needed
	// NOTE: currently, list access is not concurrent
	if (!k->ktb_ctx) { k->ktb_ctx = (void *) list_create(); }
	protobuf_list = k->ktb_ctx;

	// append context to end of the list
	(void) list_mvrear(protobuf_list);
	if (!list_insert_after(protobuf_list, ctx, 0)) {
		// TODO: should we make all of these error returns set errno, or none?
		errno = K_EINTERNAL;
		return (K_EINTERNAL);
	}

	return (K_OK);
}


/*
 * This cleans the data structure for re-use, p is a ptr returned from a
 * previous ki_create call. p continues to be valid after this call.
 */
kstatus_t
ki_clean(void *p)
{
	ktb_t *k;

	debug_printf("KI_CLEAN  : %p\n", p);
	if (!ktb_isvalid(p)) { return (K_EINVAL); }

	k = ktb_base(p);
	if (k->ktb_ctx && k->ktb_destroy) {
		// list_destroy calls our supplied destructor on elements of `ktb_ctx`
		list_destroy((LIST *) k->ktb_ctx, (void *) k->ktb_destroy);

		// de-init; a new list will be alloc'd if/when this ktb is reused
		k->ktb_ctx = NULL;
	}

	return (K_OK);
}

/*
 * completely cleans and then frees up the buffer, p is a ptr returned from a
 * previous ki_create call. p is no longer valid after this call. 
 */ 
kstatus_t
ki_destroy(void *p)
{
	ktb_t *k;

	debug_printf("KI_DESTROY: %p\n", p);
	if (ki_clean(p) != K_OK) {
		return (K_EINVAL);
	}

	k = ktb_base(p);

	/* additional destruction is required */
	switch(k->ktb_type) {
	case KITER_T:
		i_iterdestroy((kiter_t *) p);
		break;
	default:
		break;
	}

	memset(k, 0xEF, k->ktb_len); /* clear the magic number at a minimum */

	KI_FREE(k);

	return(K_OK);
}

uint32_t
ki_valid(void *p)
{
	return (ktb_isvalid(p));
}

#if 0
int
main(int argc, char *argv[])
{
	ktb_t *k;
	kv_t  *p;
	int    ktd = 14;
	
	p = (kv_t *)ki_create(ktd, KV_T);
	k = ktb_base(p);
	
	printf("Size KTB: %lu\n", sizeof(ktb_t));
	printf("KTB           = %p\n", k);
	printf("KV            = %p\n", p);

	ki_clean(p);
	if (ki_isvalid(p)) {
		k = ktb_base(p);
	
		printf("KTB           = %p\n", k);
		printf("KV            = %p\n", p);
	}

	ki_destroy(p);
	if (ki_isvalid(p)) {
		k = ktb_base(p);
	
		printf("KTB           = %p\n", k);
		printf("KV            = %p\n", p);
	}

}
#endif
