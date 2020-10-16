#ifndef _KIO_H
#define _KIO_H
#include <stdint.h>

enum kio_state {
	KIO_NEW      = 0,
	KIO_SENT        ,
	KIO_RECEIVED    ,
	KIO_FAILED      ,
	KIO_TIMEOUT     ,
};

struct kio_msg {
	struct kiovec *km_msg;  /* Ptr to an ARRAY[] of kiovecs that
				   contain the header (PDU), message
				   and potentially a value or other data */
	int km_cnt;             /* number of kiovecs in km_msg ARRAY */
	int km_status;
	int km_errno;
};

enum kio_flags {
	KIOF_INIT	= 0x0000,
	KIOF_REQRESP	= 0x0001,	/* Normal case, an RPC */
	KIOF_REQONLY	= 0x0002,	/* Special case for one way traffic */
					/* mutually exclusive wrt normal case */
#define KIOF_SET(_kio, _kiof)	((_kio)->kio_flags |= (_kiof))
#define KIOF_CLR(_kio, _kiof)	((_kio)->kio_flags &= ~(_kiof))
#define KIOF_ISSET(_kio, _kiof)	((_kio)->kio_flags & (_kiof))
};

/**
 * This is a client lib and everything in kinetic is an RPC, ie. req then resp.
 * So a kio has both send (req) and recv (resp) data structures. Since
 * everything is async at this level, the sender must send in enough
 * info to process the received response.
 *
 * Message Buffers
 * Send msg kiovec buffers are allocated and filled in by the caller.
 * Receive msg kiovec buffers are allocated by the lower receive layers.
 * All kiovec buffers in the kio structure are the responsibility of the
 * caller to free, including buffers allocated by lower level receive code.
 */
struct kio {
	uint32_t kio_cmd;		/* kinetic cmd: get, put, getlog, etc
					   for debugging only */
	int64_t kio_seq;		/* kinetic sequence */

	uint32_t kio_flags;		/* Flags modifying KIO behavior */
	enum kio_state kio_state;	/* Internal status of the KIO */

	struct kio_msg kio_sendmsg;	/* fully allocated and populated
					   msg buffers for send */

	struct kio_msg kio_recvmsg;	/* passed in empty to be filled
					   by the receive code. Caller
					   responsible to free msg buffers
					   within. */

	/* Unused so far */
	void *kio_ccontext;		/* caller context */
};


/*
 * This library uses KIO vectors using the following convention.
 * Out bound messages:
 * 	Vector	Contents
 * 	  0	The Kinetic PDU
 *	  1	The packed Kinetic request message
 *	  2	(optional)The value,
 * 		It may occupy multiple elements starting at 2, which permits
 *		API callers to build up a value without copying it into a
 *		single contiguous buffer.
 * In bound messages:
 *	  0	The Kinetic PDU
 *	  1	The packed Kinetic response message 
 * 	  2	An optional value
 */
enum kio_index {
	KIOV_PDU	= 0,
	KIOV_MSG	= 1,
	KIOV_VAL	= 2,
};

#define KM_CNT_NOVAL   2
#define KM_CNT_WITHVAL 3



#endif /* _KIO_H */
