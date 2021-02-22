#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include "kinetic.h"
#include "kinetic_internal.h"

int  i_iterinit(int ktd, kiter_t *kit);
void i_iterdestroy(kiter_t *kit);

/*
 * This module implements the Kinetic Typed Buffer (KTB) infrastructure.
 * While KTB is an interal structure, there are a handful of access 
 * functions that will be used by callers. All instances of Kinetic data 
 * types used in the Kinetic API must created and destroyed by KTB's exported 
 * functions. This includes both opaque and non-opaque data structures, 
 * the full list is in ktype_t.  For example, a key value data structure(kv_t), 
 * needed for a ki_get(), is required to be created by this module  
 * and ultimately destroyed by this module.  The need for this was driven 
 * by a zero or near zero copy policy instituted in the Kinetic API. This means 
 * that internal allocated buffers containing keys, kv meta data, log data etc, 
 * are passed directly back to the caller. The caller has no way of knowing 
 * how to deallocate these buffers. By formalizing these Kinetic data structures 
 * requiring the callers to create and destroy them, permits the library the 
 * opportunity to hide and perform the details of cleaning up these internal 
 * buffers. The callers are still allowed to hang their own buffers on these
 * structures once created, but the API will not accept base structures not 
 * created via this module. Callers are still responsible for buffers they 
 * allocate and hang on these managed structures. This creates a sound rule
 * that what the API allocates, the API is responsible for deallocating 
 * and conversely what the caller allocates, the caller is responsible for 
 * deallocating.  
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
	
	k =  KI_MALLOC(l);
	if (!k) {
		return(NULL);
	}
	
	memset(k, 0, l);
	k->ktb_magic = KTB_MAGIC;
	k->ktb_type  = t;
	k->ktb_len   = l;
	k->ktb_ktd   = ktd;

	p = (void *)k->ktb_buf;

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

/*
 * This cleans the data structure for re-use, p is a ptr returned from a
 * previous ki_create call. p continues to be valid after this call.
 */
int
ki_clean(void *p)
{
	ktb_t *k;

	if (!ktb_isvalid(p)) {
		return(-1);
	}

	k = ktb_base(p);

	if (k->ktb_destroy) {
		(k->ktb_destroy)(k->ktb_ctx);
	}

	return(0);
}

/*
 * completely cleans and then frees up the buffer, p is a ptr returned from a
 * previous ki_create call. p is no longer valid after this call. 
 */ 
int
ki_destroy(void *p)
{
	ktb_t *k;

	if (ki_clean(p) < 0) {
		return(-1);
	}

	k = ktb_base(p);

	/* additional setup is required */
	switch(k->ktb_type) {
	case KRANGE_T:
		break;
			
	case KITER_T:
		i_iterdestroy((kiter_t *)p); break;
	case KBATCH_T:
		//b_batchdestroy(ktd); break;
		break;
	default:
		break;
	}

	memset(k, 0xEF, k->ktb_len); /* clear the magic number at a minimum */

	KI_FREE(k);

	return(0);
}

int
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
