#ifndef _KIO_H
#define _KIO_H

struct kiovec {
	void  *kiov_base;    /* Starting address */
	size_t kiov_len;     /* Number of bytes to transfer */
};

struct kv {
	struct kiovec 	*kv_key; 	/* Key is an array of kiovec */
	int 		kv_key_kiovcnt;	/* Size of key kiovec array */
	
	struct kiovec 	*kv_value;	/* Value is an array of kiovec */
	int		kv_value_iovcnt;/* Size of value kiovec array */
};

enum kio_state {
	KIO_NEW = 0,
	KIO_SENT,
	KIO_RECEIVED,
	KIO_FAILED,
	KIO_TIMEOUT,
};

struct kio_msg {
	struct kiovec *km_msg;	/* Ptr to an ARRAY[] of kiovecs that 
				   contain the header (PDU), message 
				   and potentially a value or other data */
	int km_cnt;		/* number of kiovecs in km_msg ARRAY */
	int km_status;
	int km_errno;
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
	uint32_t kio_cmd;   		/* kinetic cmd: get, put, getlog, etc 
					   for debugging only */
	int64_t kio_seq;		/* kinetic sequence */

	enum kio_state kio_state;	/* Internal status of the KIO */
	
	struct kio_msg kio_sendmsg;	/* fully allocated and populated
					   msg buffers for send */
	
	struct kio_msg kio_recvmsg;	/* passed in empty to be filled
					   by the receive code. Caller 
					   responsible to free msg buffers
					   within. */

	/* Unused so far */
	void *kio_ccontext; 	/* caller context */
};



#endif /* _KIO_H */
