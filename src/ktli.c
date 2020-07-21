/*
 * KTLI - Kinetic Transport Layer Interface
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>

#include "kinetic.h"
#include "ktli.h"
#include "ktli_session.h"

/* 
 * *******  KTLI DRIVER TABLE *******
 * The driver table is where backend drivers register themselves.  Currently, 
 * these are compile time static registrations.  KTLI could use a shared 
 * object model but currently the number of backends planned are so limited 
 * in number it is not clear there is a benefit to the flexibility of shared 
 * object model. 
 * 
 * Registration:
 * A driver must add an new ID to the ktli_driver_id enum in ktli.h, populate 
 * and export a ktli_driver_fns structure and then add an entry in the 
 * KTLI Driver Table. 
 */
extern struct ktli_driver_fns socket_fns;
//extern struct ktli_driver_fns dpdk_fns;
//extern struct ktli_driver_fns uring_fns;

struct ktli_driver {
	enum ktli_driver_id	ktlid_id; 	/* driver id */
	const char 		*ktlid_name;	/* driver name */
	const char 		*ktlid_desc;	/* driver description */
	struct ktli_driver_fns 	*ktlid_fns;	/* driver functions */
	//	int			ktlid_flags;
};

/* KTLI Driver table */
static struct ktli_driver ktlid_table[] = {
	{ KTLI_DRIVER_SOCKET, "ktli_socket", "Linux socket driver", &socket_fns },
//	{ KTLI_DRIVER_DPDK,   "ktli_dpdk",   "Linux dpdk driver",   &dpdk_fns },
//	{ KTLI_DRIVER_URING,  "ktli_uring",  "Linux io_uring driver", &uring_fns },

	/* Must be Last */
	{ KTLI_DRIVER_NONE,   "EOT", "End of Table", NULL },
};
	
/* predeclare thread creation target functions */
static void *ktli_sender(void *p);
static void *ktli_receiver(void *p);

static int ktli_up = 0; /* global used to lazy init KTLI */

/* Abstracting malloc and free, permits testing  */ 
#define KTLI_MALLOC(_l) malloc((_l))
#define KTLI_FREE(_p) free((_p))

/*
 * internal lazy init function, called on first open
 */
static void
ktli_init()
{
	kts_init();
}

/**
 * int ktli_open(enum ktli_driver_id did, struct ktli_helpers *kh)
 *
 * This function opens a new ktli session.  The open binds a session
 * to a backend driver and provides protocol specific helps for the
 * session.
 * 
 * @param did A KTLI driver id the identifies the requested back end driver 
 * @param kh  A set of helper functions and data that divorces KTLI from proto
 */ 
int
ktli_open(enum ktli_driver_id did,
	  struct ktli_config *cf,
	  struct ktli_helpers *kh)
{
	int i, rc;
	int *kts;
	void *res;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	struct ktli_queue *sq, *rq, *cq;
	pthread_t stid, rtid;
	
	/* Lazy module init - must only happen once - SBCAS guarantees */
	if (SBCAS(&ktli_up, 0, 1))
		ktli_init();

	/* 
	 * Validate arguments.
	 * Find the driver table entry for the passed in did 
	 * Ensure helper functions are available
	 */
	i=0;
	de = NULL;
	while (ktlid_table[i].ktlid_id != KTLI_DRIVER_NONE) {
		if (ktlid_table[i].ktlid_id == did) {
			de = &ktlid_table[i];
			break;
		}
		i++;
	}

	if (!de || !kh ||
	    !kh->kh_getaseq_fn		||
	    !kh->kh_setseq_fn		||
	    !kh->kh_msglen_fn 		||
	    kh->kh_recvhdr_len <= 0	||
	    kh->kh_recvhdr_len >  1024) {
		errno = EINVAL;
		return(-1);
	}
	
	assert(de->ktlid_fns);
	assert(de->ktlid_fns->ktli_dfns_open);

	/* need to allocate kts so that it can be passed to threads */
	kts = KTLI_MALLOC(sizeof(int));

	/* Allocate sender, receiver and completion Qs */
	sq = (struct ktli_queue *)KTLI_MALLOC(sizeof(struct ktli_queue));
	rq = (struct ktli_queue *)KTLI_MALLOC(sizeof(struct ktli_queue));
	cq = (struct ktli_queue *)KTLI_MALLOC(sizeof(struct ktli_queue));
	
	if (sq) {
		sq->ktq_list = list_init();
		pthread_mutex_init(&sq->ktq_m, NULL);
		pthread_cond_init(&sq->ktq_cv, NULL);
	}
	
	if (rq) {
		rq->ktq_list = list_init();
		pthread_mutex_init(&rq->ktq_m, NULL);
		pthread_cond_init(&rq->ktq_cv, NULL);
	}
	
	if (cq) {
		cq->ktq_list = list_init();
		pthread_mutex_init(&cq->ktq_m, NULL);
		pthread_cond_init(&cq->ktq_cv, NULL );
	}
	
	if (!kts || !sq || !rq || !cq ||
	    !sq->ktq_list || !rq->ktq_list || !cq->ktq_list) {
		/* undo any successful allocations */
		(void)(sq->ktq_list?list_free(sq->ktq_list, LIST_NODEALLOC):0);
		(void)(rq->ktq_list?list_free(rq->ktq_list, LIST_NODEALLOC):0);
		(void)(cq->ktq_list?list_free(cq->ktq_list, LIST_NODEALLOC):0);
		(void)(sq?KTLI_FREE(sq):0);
		(void)(rq?KTLI_FREE(rq):0);
		(void)(cq?KTLI_FREE(cq):0);
		(void)(kts?KTLI_FREE(kts):0);
		errno = ENOMEM;
		return(-1);
	}

	sq->ktq_exit = 0;
	rq->ktq_exit = 0;
	cq->ktq_exit = 0;
	
	/* Now allocate a session slot, alloc sets driver */
	*kts = kts_alloc_slot();
	if (*kts < 0) {
		/* exhausted ktss */
		errno = EMFILE;
		goto open_err;
	}

	/* call the corresponding driver open */
	dh = (de->ktlid_fns->ktli_dfns_open)();
	if (!dh) {
		/* failed, so release the slot */
		errno = EMFILE;
		goto open_err;
	}

	/* Set the driver, dhandle config and helpers */
	kts_set_driver(*kts, de);
	kts_set_dhandle(*kts, dh);
	kts_set_config(*kts, cf);
	kts_set_helpers(*kts, kh);
	
	/* Save the session Qs */
	kts_set_sendq(*kts, sq);
	kts_set_recvq(*kts, rq);
	kts_set_compq(*kts, cq);

	/* start the sender and receiver threads */
	rc = pthread_create(&stid, NULL, ktli_sender, kts);
	if (rc) {
		/* failed */
		goto open_err;
	}
	kts_set_sender(*kts, stid);
	
	rc = pthread_create(&rtid, NULL, ktli_receiver, kts);
	if (rc) {
		/* failed, so signal sender to exit */
		pthread_mutex_lock(&sq->ktq_m);
		sq->ktq_exit = 1;
		pthread_cond_signal(&sq->ktq_cv);
		pthread_mutex_unlock(&sq->ktq_m);
		pthread_join(stid, &res);

		goto open_err;
	}
	kts_set_receiver(*kts, rtid);

	/* New session, start the message sequence number at 0 */
	kts_set_sequence(*kts, 100);
	
	/* We're open for business */
	kts_set_state(*kts, KTLI_SSTATE_OPENED); 

	return(*kts);

 open_err:
	/* free the list, no elements yet so set LIST_NODEALLOC */
	list_free(sq->ktq_list, LIST_NODEALLOC);
	list_free(rq->ktq_list, LIST_NODEALLOC);
	list_free(cq->ktq_list, LIST_NODEALLOC);
	KTLI_FREE(sq);
	KTLI_FREE(rq);
	KTLI_FREE(cq);
	kts_free_slot(*kts);
	KTLI_FREE(kts);
	return(-1);
}

/**
 * int ktli_close(int kts)
 *
 * This function closes an opened ktli session. Can only close an open
 * session, connected or aborted sessions must be disconnected first.
 * Close also requires all of the session queues to be empty. Close
 * deallocates all session resources and releasing the session slot. 
 *
 * @param kts A KTLI session id. 
 * 
 * PAK:  Need to destroy mutexex and cond vars
 */
int
ktli_close(int kts)
{
	int rc;
	void *res;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	struct ktli_queue *q;
	enum ktli_sstate st;
	pthread_t tid;

	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}
	st = kts_state(kts);
	
	/* only 4 states Unknown, Opened, Connected or Aborted */
	switch (st) {
	case KTLI_SSTATE_UNKNOWN:
		/* Unknown, nothing to do */
		return(0);
		
	case KTLI_SSTATE_OPENED:
		/* a good state, so let's close */
		break;

	case KTLI_SSTATE_CONNECTED:
	case KTLI_SSTATE_ABORTED:
		/* Need to Disconnect first */
		errno = EISCONN;
		return(-1);
		
	case KTLI_SSTATE_DRAINING:
		/* Need to drain first */
		errno = EBADFD;
		return(-1);
		
	default:
		/* should never get here */
		assert(0);
		return(-1);
	} 

	/* 
	 * PAK: need to implement a ktli_drain API to drain the queues 
	 * Can't free the kio with out endandering the caller, caller 
	 * must do this
	 */
	if (list_size((kts_sendq(kts))->ktq_list) ||
	    list_size((kts_recvq(kts))->ktq_list) ||
	    list_size((kts_compq(kts))->ktq_list))  {
		    errno = ENOTEMPTY;
		    return(-1);
	}
	
	dh = kts_dhandle(kts);
	de = kts_driver(kts);

	if (!de || !dh) {
		/* invalid kts */
		errno = EBADF;
		return(-1);
	}

	assert(de->ktlid_fns);
	assert(de->ktlid_fns->ktli_dfns_close);

	/* call the corresponding driver close */
	rc = (de->ktlid_fns->ktli_dfns_close)(dh);
	if (rc == -1) {
		return(-1);
	}

	/* close down the sender thread */
	q = kts_sendq(kts);
	tid = kts_sender(kts);
	
	pthread_mutex_lock(&q->ktq_m);
	q->ktq_exit = 1;
	pthread_cond_signal(&q->ktq_cv);
	pthread_mutex_unlock(&q->ktq_m);
	pthread_join(tid, &res);
	printf("Sender: %p\n",res);

	/* free the sender list and queue */
	/* Pull any kio's off the list and free them */
	/* PAK : this is unecessary code */
	while (list_size(q->ktq_list)) {
		KTLI_FREE(list_remove_rear(q->ktq_list));
	}
	
	/* free the list and the queue */
	list_free(q->ktq_list, LIST_NODEALLOC);
	KTLI_FREE(q);

	/* close down the receiver thread */
	q = kts_recvq(kts);
	tid = kts_receiver(kts);
	
	pthread_mutex_lock(&q->ktq_m);
	q->ktq_exit = 1;
	pthread_cond_signal(&q->ktq_cv);
	pthread_mutex_unlock(&q->ktq_m);
	pthread_join(tid, &res);
	printf("Receiver: %p\n",res);

	/* free the receiver list and queue*/
	/* Pull any kio's off the list and free them */
	/* PAK: unecessary code */
	while (list_size(q->ktq_list)) {
		KTLI_FREE(list_remove_rear(q->ktq_list));
	}
	
	/* free the list and the queue */
	list_free(q->ktq_list, LIST_NODEALLOC);
	KTLI_FREE(q);
	
	/* Free up the completion queue */
	q = kts_compq(kts);
	
	/* Pull any kio's off the list and free them */
	/* PAK: Unecessary code */
	while (list_size(q->ktq_list)) {
		KTLI_FREE(list_remove_rear(q->ktq_list));
	}
	
	/* free the list and the queue */
	list_free(q->ktq_list, LIST_NODEALLOC);
	KTLI_FREE(q);

	/* 
	 * res is the original argument to the threads, a ptr to the
	 * the session descriptor, kts. Both receiver and sender use 
	 * the same ptr. Allocated in ktli_open() so free it here in 
	 * ktli_close(). Equivalent to the free(kts) in ktli_open() 
	 * at open_err:
	 */
	KTLI_FREE(res);
	
	/* free the session slot */
	kts_free_slot(kts);
	
	return(0);
}

/**
 * int ktli_connect(int kts, 
 *                  char *host, char *port, int usetls, int id, char *hmac)
 *
 * This function connects an open session to a server. The session 
 * must be already opened. The parameters specifiy the server, port
 * desire for tls and a user id/hmac pair.   
 *
 * @param kts	An opened KTLI session id. 
 * @param host	A host string that could be a dotted IP, hostname or FQDN 
 * @param post	A port string that could an ascii number or service name
 * @param usetls A boolean indicating whether to use TLS or not
 * @param id	A Kinetic user ID
 * @param hmac	A Kinetic HMAC 
 * 
 * PAK: Create KTLI config struct and hang them off the session for logging
 */
int
ktli_connect(int kts)
{
	int rc;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	enum ktli_sstate st;
	struct ktli_config *cf;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}
		
	dh = kts_dhandle(kts);
	de = kts_driver(kts);
	st = kts_state(kts);
	cf = kts_config(kts);
	
	if (st != KTLI_SSTATE_OPENED) {
		/* invalid kts */
		errno = EBADFD;
		return(-1);
	}

	if (!de || !dh) {
		/* invalid kts */
		errno = EBADF;
		return(-1);
	}

	assert(de->ktlid_fns);
	assert(de->ktlid_fns->ktli_dfns_connect);

	/* call the corresponding driver connect */
	rc = (de->ktlid_fns->ktli_dfns_connect)(dh, cf->kcfg_host,
						cf->kcfg_port,
						(cf->kcfg_flags & KCFF_TLS));
	if (rc == -1) {
		/* driver connect sets errno */
		return(-1);
	}

	kts_set_state(kts, KTLI_SSTATE_CONNECTED); 

	return(0);
}

/**
 * int ktli_connect(int kts, 
 *                  char *host, char *port, int usetls, int id, char *hmac)
 *
 * This function disconnects a connected session. It will notify any thread
 * polling the session that the session has been disconnected. 
 *
 * @param kts	An opened KTLI session id. 
 */
int
ktli_disconnect(int kts)
{
	int rc;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	enum ktli_sstate st;
	struct ktli_queue *cq;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}
		
	/* Must be either connected or aborted to disconnect */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_CONNECTED) && (st != KTLI_SSTATE_ABORTED)) {
		errno = ENOTCONN;
		return(-1);
	}

	dh = kts_dhandle(kts);
	de = kts_driver(kts);

	if (!de || !dh ) {
		/* invalid kts */
		errno = EBADF;
		return(-1);
	}

	assert(de->ktlid_fns);
	assert(de->ktlid_fns->ktli_dfns_disconnect);

	/* call the corresponding driver disconnect */
	rc = (de->ktlid_fns->ktli_dfns_disconnect)(dh);
	if (rc == -1) {
		return(-1);
	}
	
	kts_set_state(kts, KTLI_SSTATE_DRAINING);
	
	/* If anyone is polling wake them up */
	cq = kts_compq(kts);
	pthread_mutex_lock(&cq->ktq_m);
	pthread_cond_broadcast(&cq->ktq_cv);
	pthread_mutex_unlock(&cq->ktq_m);
	
	return(0);
}

/**
 * int ktli_send(int kts, struct kio *kio)
 *
 * This function asynchronously sends a kio, which contains a kinetic 
 * request, to the connected kinetic server. The kio provides enough
 * information for the complete send receive processing nof the kinetic 
 * request. The function returns once the the request has been validated 
 * and queued for send service.
 * 
 * @param kts An opened and connected kinetic session descriptor. 
 * @param kio A filled in kio structure that contains a servicable
 * 		kinetic request.
 */ 
int
ktli_send(int kts, struct kio *kio)
{
	char *rc;
	struct ktli_queue *sq;
	enum ktli_sstate st;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}
			
	/* verify kts is connected */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_CONNECTED)) {
		errno = ENOTCONN;
		return(-1);
	}
	
	/* Validate kio - Send msg is mandatory */
	if (!kio->kio_sendmsg.km_cnt || !kio->kio_sendmsg.km_msg) {
		errno = EINVAL;
		return(-1);
	}

	/* initialize unused kio elements */
	kio->kio_state = KIO_NEW;
	kio->kio_sendmsg.km_status = 0;
	kio->kio_sendmsg.km_errno = 0;
	memset(&kio->kio_recvmsg, 0, sizeof(struct kio_msg));

	/* grab the sendq */
	sq = kts_sendq(kts);
	if (!sq) {
		/* invalid kts */
		errno = EBADF;
		return(-1);
	}

	pthread_mutex_lock(&sq->ktq_m);

	/* queue it on the end */
	(void)list_mvrear(sq->ktq_list);
	if (!list_insert_after(sq->ktq_list,
			       (char *)&kio, sizeof(struct kio *))) {
		errno = ENOMEM;
		pthread_mutex_unlock(&sq->ktq_m);
		return(-1);
	}
	
	/* wake up the sender */
	pthread_cond_signal(&sq->ktq_cv);
	
	pthread_mutex_unlock(&sq->ktq_m);

	return(0);
}

/* 
 * List helper function to find a matching kio given a seq number
 * Return 0 for true or a match
 */
static int
ktli_kiomatch(char *data, char *ldata)
{
	int match = -1;
	struct kio *kio = (struct kio *)data;
	struct kio *lkio = *(struct kio **)ldata;

	if (kio == lkio)
		match = 0;
	return (match);
}

/* 
 * List helper function to find a kio with no req sendmsg
 * Return 0 for true or a match
 */
static int
ktli_kionoreq(char *data, char *ldata)
{
	int match = -1;
	struct kio *lkio = *(struct kio **)ldata;

	if (lkio->kio_sendmsg.km_cnt == 0) /* sendmsg vector - no req */
		match = 0;
	return (match);
}

/**
 * int ktli_receive(int kts, struct kio *kio)
 *
 * This function receives any completed kio. If no kio are completed it
 * immediately returns back to the caller. A received kio is hung on
 * the caller provided kio ptr. 
 * 
 * @param kts An opened and connected kinetic session descriptor. 
 * @param kio A kio ptr. The completed queue is searched for the passed in kio 
 *            if found, it is removed and success returned, failure otherwise.
 *
 */ 
int
ktli_receive(int kts, struct kio *kio)
{
	int rc = 0;
	enum ktli_sstate st;
	struct ktli_queue *cq;
	struct kio **lkio;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}

	/* 
	 * verify kts is connected or in draining, receives can drain
	 * as well as ktli_drain, the difference is that the receive must 
	 * match a kio to drain and since we are unconnected a draining
	 * receive can receive from all queues in compq, recvq, sendq order.  
	 */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_CONNECTED) && (st != KTLI_SSTATE_DRAINING)) {
		errno = ENOTCONN;
		return(-1);
	}

	/* 
	 * This is a special case, the connection is gone and we need to
	 * receive all the failed incomplete requests. 
	 */
	if ((st == KTLI_SSTATE_DRAINING)) {
		rc = ktli_drain_match(kts, kio);
		return(rc);
	}

	cq = kts_compq(kts);

	pthread_mutex_lock(&cq->ktq_m);

	/* list_traverse defaults to starting the traverse at the front */
	rc = list_traverse(cq->ktq_list, (char *)kio, ktli_kiomatch, LIST_ALTR);
	if (rc == LIST_EXTENT || rc == LIST_EMPTY) {
		errno = ENOENT;
		rc = -1;
	} else {
		/* Found the requested kio */
		lkio = (struct kio **)list_remove_curr(cq->ktq_list);
		assert(kio = *lkio);
		KTLI_FREE(lkio);
		rc = 0;
	}
	/* leave the list ready for an insert */
	(void)list_mvrear(cq->ktq_list);

	/* if there are still messages wakeup the next */
	if (list_size(cq->ktq_list)) {
		pthread_cond_broadcast(&cq->ktq_cv);
	}

	pthread_mutex_unlock(&cq->ktq_m);
	
	return(rc);
}

/**
 * int ktli_receive(int kts, struct kio *kio)
 *
 * This function receives any completed kio. If no kio are completed it
 * immediately returns back to the caller. A received kio is hung on
 * the caller provided kio ptr. 
 * 
 * @param kts An opened and connected kinetic session descriptor. 
 * @param kio A kio ptr. The completed queue is searched for the passed in kio 
 *            if found, it is removed and success returned, failure otherwise.
 *
 */ 
int
ktli_receive_unsolicited(int kts, struct kio **kio)
{
	int rc = 0;
	enum ktli_sstate st;
	struct ktli_queue *cq;
	struct kio **lkio;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}

	/* 
	 * verify kts is connected or in draining, receives can drain
	 * as well as ktli_drain, the difference is that the receive must 
	 * match a kio to drain and since we are unconnected a draining
	 * receive can receive from all queues in compq, recvq, sendq order.  
	 */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_CONNECTED) && (st != KTLI_SSTATE_DRAINING)) {
		errno = ENOTCONN;
		return(-1);
	}

	cq = kts_compq(kts);

	pthread_mutex_lock(&cq->ktq_m);

	/* list_traverse defaults to starting the traverse at the front */
	rc = list_traverse(cq->ktq_list, (char *)kio, ktli_kionoreq, LIST_ALTR);
	if (rc == LIST_EXTENT || rc == LIST_EMPTY) {
		errno = ENOENT;
		rc = -1;
	} else {
		/* Found the requested kio */
		lkio = (struct kio **)list_remove_curr(cq->ktq_list);
		*kio = *lkio;
		KTLI_FREE(lkio);
		rc = 0;
	}
	/* leave the list ready for an insert */
	(void)list_mvrear(cq->ktq_list);

	/* if there are still messages wakeup the next */
	if (list_size(cq->ktq_list)) {
		pthread_cond_broadcast(&cq->ktq_cv);
	}

	pthread_mutex_unlock(&cq->ktq_m);
	
	return(rc);
}

/**
 * int ktli_poll(int kts, int timeout)
 *
 * This function polls a connected session to see if there are any 
 * completed and receivable kios.  Will block till either a kio 
 * becomes ready or until the session is disconnected.
 * 
 * @param kts A connected kinetic session descriptor. 

 *
 * PAK: timeout not implemented  
*/
int
ktli_poll(int kts, int timeout)
{
	enum ktli_sstate st;
	struct ktli_queue *cq;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}

	/* verify kts is connected */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_CONNECTED)) {
		errno = ENOTCONN;
		return(-1);
	}

	cq = kts_compq(kts);

	pthread_mutex_lock(&cq->ktq_m);
	if (!list_size(cq->ktq_list))
	    pthread_cond_wait(&cq->ktq_cv, &cq->ktq_m);
	pthread_mutex_unlock(&cq->ktq_m);
       
	return(1);
}

/**
 * int ktli_drain(int kts, struct kio **kio)
 *
 * This function drains adraining session of queued kio's.
 * This must be done to successfully close a session.  Each call to drain
 * will return a single previously queued kio.  Once the queues are empty
 * it will return a NULL kio. Completed queues are drained first, then
 * the receive queue and then the send queue.  
 * 
 * @param kts A connected kinetic session descriptor. 
 * @param kio A PTR to a kio PTR. drain will return a single dequeued kio 
 *	      on this ptr.
 */
int
ktli_drain(int kts, struct kio **kio)
{
	int rc, i;
	enum ktli_sstate st;
	struct kio **lkio;
	struct ktli_queue *q;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}

	/* verify kts is set to drain */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_DRAINING)) {
		errno = EBADFD;
		return(-1);
	}

	rc = 0;
	*kio = NULL;
	for (i=0; i<3; i++) {
		switch (i) {
		case 0: q = kts_compq(kts); break;
		case 1: q = kts_recvq(kts); break;
		case 2: q = kts_sendq(kts); break;
		}
		
		pthread_mutex_lock(&q->ktq_m);
		if (list_size(q->ktq_list)) {
			lkio = (struct kio **)list_remove_front(q->ktq_list);
			*kio = *lkio;
			KTLI_FREE(lkio);
			rc = 1;
		}
		pthread_mutex_unlock(&q->ktq_m);

		if (*kio) break; /* De-queued a kio, so return */
	}

	/* if queues are empty move to opened */
	if (!(*kio))
		kts_set_state(kts,  KTLI_SSTATE_OPENED);
	
	return(rc);
}

/**
 * int ktli_drain_match(int kts, struct kio **kio)
 *
 * This function drains a draining session of queued kio's.
 * This must be done to successfully close a session.  Each call to drain
 * match will locate a provided kio in one of the three queues, dequeue it 
 * and return success that it was found and dequeued. Search order
 * is compq, recvq, and then sendq. 
 * 
 * @param kts A connected kinetic session descriptor. 
 * @param kio A kio PTR. drain match will match the provided kio to onea kio  
 *        in either the compq , recvq or sendq.
 */
int
ktli_drain_match(int kts, struct kio *kio)
{
	int rc, i;
	int tqlen; /* sum of all q sizes */
	enum ktli_sstate st;
	struct kio **lkio;
	struct ktli_queue *q;
	
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}

	/* verify kts is set to drain */
	st = kts_state(kts);
	if ((st != KTLI_SSTATE_DRAINING)) {
		errno = EBADFD;
		return(-1);
	}

	/*
	 * This loop does 2 things: first  it looks for a specific
	 * named kio to drain across all three q's.  If found, it dequeues 
	 * it, and sets a successful return code. Second it determines the 
	 * total number of kio's remaining across all three q's. If no 
	 * KIOs left, then move the session state to OPENED.
	 */
	tqlen = 0;
	rc = -1;
	for (i=0; i<3; i++) {
		switch (i) {
		case 0: q = kts_compq(kts); break;
		case 1: q = kts_recvq(kts); break;
		case 2: q = kts_sendq(kts); break;
		}
		
		pthread_mutex_lock(&q->ktq_m);

		/* list_traverse defaults to starting at the front */
		rc = list_traverse(q->ktq_list, (char *)kio,
				   ktli_kiomatch, LIST_ALTR);

		if (rc != LIST_EXTENT && rc != LIST_EMPTY) {
			/* Found the requested kio */
			lkio = (struct kio **)list_remove_curr(q->ktq_list);
			assert(kio = *lkio);
			KTLI_FREE(lkio);
			rc = 0;
		}

		tqlen += list_size(q->ktq_list);
		
		/* leave the list ready in a nominal position */
		(void)list_mvrear(q->ktq_list);

		pthread_mutex_unlock(&q->ktq_m);
	}

	/* if queues are empty move to opened */
	if (!tqlen)
		kts_set_state(kts,  KTLI_SSTATE_OPENED);
	
	return(rc);
}
/**
 * int ktli_pol (int kts, struct ktli_config *cf)
 *
 * This function returns the session config structure 
 * 
 * @param kts A connected kinetic session descriptor. 
 *
*/
int
ktli_config(int kts, struct ktli_config **cf)
{
	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}
		
	*cf = kts_config(kts);
	return(0);
}

/*
 * Session thread functions 
 */
static void *
ktli_sender(void *p)
{
	int rc;
	int kts;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	struct ktli_helpers *kh;
	struct ktli_queue *sq;
	struct ktli_queue *rq;
	struct ktli_queue *cq;
	struct kio *kio, **lkio;
	
	assert(p);

	kts = *(int *)p;
	dh = kts_dhandle(kts);
	de = kts_driver(kts);
	kh = kts_helpers(kts);
	sq = kts_sendq(kts);
	rq = kts_recvq(kts);
	cq = kts_compq(kts);
	
	assert(dh);
	assert(de);
	assert(de->ktlid_fns);
	assert(de->ktlid_fns->ktli_dfns_send);

	printf("Sender: starting %d (%p)\n", kts, p);
	
	/* Main processing loop, continue until told to leave */
	do {
		/* Wait for an event */
		pthread_mutex_lock(&sq->ktq_m);
		
		/* 
		 * a kio could be added after the process queue while loop
		 * terminated and this point. Also the exit flag could have
		 * been raised. So only if the send queue is empty and no 
		 * exit is signalled, should we wait
		 */
		if (!list_size(sq->ktq_list) && !sq->ktq_exit)
			/* Empty Q, need to wait. */
			pthread_cond_wait(&sq->ktq_cv, &sq->ktq_m);

		pthread_mutex_unlock(&sq->ktq_m);
		
		printf("Sender: caught signal\n");
		
		if (sq->ktq_exit) break;

		/* Process the send queue */
		while (list_size(sq->ktq_list)) {
			
			pthread_mutex_lock(&sq->ktq_m);
			lkio = (struct kio **)list_remove_front(sq->ktq_list);
			kio = *lkio;
			KTLI_FREE(lkio);
			pthread_mutex_unlock(&sq->ktq_m);
			
			/* Use current session seq for this kio */
		        kio->kio_seq = kts_sequence(kts);

			/* bump the session seq for the next message */
			kts_set_sequence(kts, kio->kio_seq + 1); 
			
			(kh->kh_setseq_fn)(kio->kio_sendmsg.km_msg,
					     kio->kio_sendmsg.km_cnt,
					     kio->kio_seq);

			/* 
			 * call the corresponding driver send fn
			 * lower driver is concerned with ensuring all
			 * bytes are sent
			 */ 
			rc = (de->ktlid_fns->ktli_dfns_send)(dh,
						       kio->kio_sendmsg.km_msg,
						       kio->kio_sendmsg.km_cnt);

			kio->kio_sendmsg.km_status = rc;
			kio->kio_sendmsg.km_errno = errno;

			if (rc < 0) {
				/* Error, hang it on the completed Q */
				printf("KTLI Send Error\n");
				kio->kio_state = KIO_FAILED;
				
				pthread_mutex_lock(&cq->ktq_m);
				(void)list_mvrear(cq->ktq_list);
				list_insert_before(cq->ktq_list,
					   (char *)&kio, sizeof(struct kio *));
				pthread_cond_broadcast(&cq->ktq_cv);
				pthread_mutex_unlock(&cq->ktq_m);

				continue;
			}
			
			/* Success */
			kio->kio_state = KIO_SENT;

			/* Hang it on the receive queue */
			pthread_mutex_lock(&rq->ktq_m);
			(void)list_mvrear(rq->ktq_list);
			list_insert_before(rq->ktq_list,
					   (char *)&kio, sizeof(struct kio *));

			/* wakeup the receiver */
			pthread_cond_signal(&rq->ktq_cv);
			pthread_mutex_unlock(&rq->ktq_m);
		}
		
	} while (1); /* forever */
	
	printf("Sender: exiting\n");

	pthread_exit(p);
}

/* 
 * List helper function to find a matching kio given a seq number
 * Return 0 for true or a match
 */
static int
ktli_seqmatch(char *data, char *ldata)
{
	int match = -1;
	int64_t *aseq = (int64_t *)data;
	struct kio *lkio = *(struct kio **)ldata;

	if (*aseq == lkio->kio_seq)
		match = 0;
	return (match);
}

/**
 * ktli_recvmsg(int kts) 
 *
 * This is helper function for the main receiver loop. It does all the 
 * work to detect and receive a message. It matches up the received 
 * message with a pending request.  Successful results are placed 
 * on the completion queue. This is only called by the receiver thread. 
 *
 * ERRORS: Since the response message stream is a byte stream, errors 
 * in this routine can be viewed as catastrophic. One out of sync with
 * the server due to a failed KTLI_MALLOC or receive the entire connection is
 * compromised.  Although you may be able to recover from a failed 
 * read by scanning for the next magic number of a header, the other reasons
 * for a failing, like KTLI_MALLOC, are most likely unrecoverable. 
 * So error recovery here is to set the session state to ABORTED 
 * disconnect and let the client cleanup. 
 * Need to handle EAGAIN, EWOULDBLOCK, EINTR t

 * 
 * @param kts An opened and connected kinetic session descriptor. 
 */ 

static int
ktli_recvmsg(int kts)
{
	int rc;
	int64_t aseq;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	struct ktli_helpers *kh;
	struct ktli_queue *sq;
	struct ktli_queue *rq;
	struct ktli_queue *cq;
	struct kio *kio, **lkio;
	struct kio_msg msg;
	
	dh = kts_dhandle(kts);
	de = kts_driver(kts);
	kh = kts_helpers(kts);
	sq = kts_recvq(kts);
	rq = kts_recvq(kts);
	cq = kts_compq(kts);

	/* 
	 * We have a message, time to receive it. 
	 * Need to allocate a 2 element kiovec array, 
	 * one kiovec for the header[0] and 
	 * another kiovec for the message[1] 
	 */
	msg.km_msg = KTLI_MALLOC(sizeof(struct kiovec) * 2);
	if (!msg.km_msg) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: KTLI_MALLOC failed\n", __FILE__, __LINE__);
		goto recvmsgerr;
	}

	/* Allocate the header buffer */
	msg.km_msg[0].kiov_base = KTLI_MALLOC(kh->kh_recvhdr_len);
	if (!msg.km_msg[0].kiov_base) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: KTLI_MALLOC failed\n", __FILE__, __LINE__);
		goto recvmsgerr;
	}
	msg.km_msg[0].kiov_len = kh->kh_recvhdr_len;
	
	/* call the corresponding driver receive to get the header */
	rc = (de->ktlid_fns->ktli_dfns_receive)(dh, &msg.km_msg[0], 1);
	if (rc == -1) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: receive failed %d\n", __FILE__, __LINE__, rc);
		goto recvmsgerr;
	}

	/* call helper fn to get the total message length */
	msg.km_msg[1].kiov_len = (kh->kh_msglen_fn)(&msg.km_msg[0]);
	if (msg.km_msg[1].kiov_len < 0) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: Helper msglen failed failed\n",
		       __FILE__, __LINE__);
		goto recvmsgerr;
	}
		
	/* Now reduce by the header we already received */
	/* msg.km_msg[1].kiov_len -= kh->kh_recvhdr_len; */

	/* Allocate the message buffer */
	msg.km_msg[1].kiov_base = KTLI_MALLOC(msg.km_msg[1].kiov_len);
	if (!msg.km_msg[1].kiov_base) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: KTLI_MALLOC failed\n", __FILE__, __LINE__);
		goto recvmsgerr;
	}

	/* call the corresponding driver receive to get the message */
	rc = (de->ktlid_fns->ktli_dfns_receive)(dh, &msg.km_msg[1], 1);
	if (rc == -1) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: receive failed %d\n", __FILE__, __LINE__, errno);
		perror("");
		goto recvmsgerr;
		assert(0);
	}

	/* We have the message. Find its matching request */
	aseq = (kh->kh_getaseq_fn)(msg.km_msg, 2);

	/* search through the recvq, need the mutex */
	pthread_mutex_lock(&rq->ktq_m);
	rc = list_traverse(rq->ktq_list,
			   (char *)&aseq, ktli_seqmatch, LIST_ALTR);
	if (rc == LIST_EXTENT || rc == LIST_EMPTY) {
		/*
		 * No matching kio.  Allocate a kio, set it up and mark it as
		 * received. Let the upper layers deal with it.
		 */
		kio = KTLI_MALLOC(sizeof(struct kio));
		if (!kio) {
			printf("%s:%d: KTLI_MALLOC failed\n",
			       __FILE__, __LINE__);
			goto recvmsgerr;
		}
		memset((void *)kio, 0, sizeof(struct kio));
		kio->kio_seq = aseq;
	} else {
		lkio = (struct kio **)list_remove_curr(rq->ktq_list);
		kio = *lkio;
		KTLI_FREE(lkio);
	}

	(void)list_mvrear(rq->ktq_list); /* reset to rear after traverse */
	pthread_mutex_unlock(&rq->ktq_m);

	if (!kio) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		printf("%s:%d: traverse failed\n", __FILE__, __LINE__);
		assert(0);
		return(-1);
	}
	       
	/* hang response onto the kio */
	kio->kio_state = KIO_RECEIVED;
	kio->kio_recvmsg.km_status = 0;
	kio->kio_recvmsg.km_errno = 0;
	kio->kio_recvmsg.km_cnt = 2;
	kio->kio_recvmsg.km_msg = msg.km_msg;
	
	/* Add to completion queue */
	pthread_mutex_lock(&cq->ktq_m);
	(void)list_mvrear(cq->ktq_list);
	list_insert_before(cq->ktq_list, (char *)&kio, sizeof(struct kio *));

	/* Let everyone know there is a new completed  kio */
	pthread_cond_broadcast(&cq->ktq_cv);
	
	pthread_mutex_unlock(&cq->ktq_m);
	return(0);

 recvmsgerr:
	/* 
	 * THIS IS CATASOTROPHIC
	 * Move to ABORTED state to prevent new sends, or other activity, 
	 * disconnect the session to facilitate cleanup will move
	 * to DRAINING, move all messages to completed queue marking them
	 * as failed. 
	 */
	
	/* set the session ABORTED STATE and disconnect*/
	kts_set_state(kts, KTLI_SSTATE_ABORTED);
	ktli_disconnect(kts);
	
	/* 
	 * Take the entire recvq and sendq and move them
	 * to compq setting the approriate errors on each  
	 * kio. Client should then drain/recv and close.
	 */
	pthread_mutex_lock(&sq->ktq_m);
	pthread_mutex_lock(&rq->ktq_m);
	pthread_mutex_lock(&cq->ktq_m);

	/* set the cq so that we add to the rear */
	(void)list_mvrear(cq->ktq_list);
			
	while (list_size(rq->ktq_list)) {
		lkio = (struct kio **)list_remove_front(rq->ktq_list);
		kio = *lkio;
		KTLI_FREE(lkio); /* created by the list */

		kio->kio_state = KIO_FAILED;
		list_insert_before(cq->ktq_list, (char *)&kio,
				   sizeof(struct kio *));
	}
			
	while (list_size(sq->ktq_list)) {
		lkio = (struct kio **)list_remove_front(sq->ktq_list);
		kio = *lkio;
		KTLI_FREE(lkio); /* created by the list */

		kio->kio_state = KIO_FAILED;
		list_insert_before(cq->ktq_list, (char *)&kio,
				   sizeof(struct kio *));
	}

	/* notify anyone sleeping on the completion queue */
	pthread_cond_broadcast(&cq->ktq_cv);
	pthread_mutex_unlock(&cq->ktq_m);
	pthread_mutex_unlock(&rq->ktq_m);
	pthread_mutex_unlock(&sq->ktq_m);

	/* free any buffers allocated */
	(msg.km_msg?KTLI_FREE(msg.km_msg):0);
	(msg.km_msg[0].kiov_base?KTLI_FREE(msg.km_msg[0].kiov_base):0);
	(msg.km_msg[1].kiov_base?KTLI_FREE(msg.km_msg[1].kiov_base):0);

	return(-1);
}

		
static void *
ktli_receiver(void *p)
{
	int rc;
	int kts;
	void *dh; 			/* driver handle */
	struct ktli_driver *de; 	/* driver entry */
	struct ktli_queue *rq;
	struct ktli_queue *cq;
	struct kio *kio;
	
	assert(p);

	kts = *(int *)p;
	dh = kts_dhandle(kts);
	de = kts_driver(kts);
	rq = kts_recvq(kts);
	cq = kts_compq(kts);
	
	assert(dh);
	assert(de);
	assert(de->ktlid_fns);
	assert(de->ktlid_fns->ktli_dfns_receive);
	assert(de->ktlid_fns->ktli_dfns_poll);

	printf("Receiver: waiting %d (%p)\n", kts, p);
	sleep(1); /* let the connection establish */
	printf("Receiver: starting %d (%p)\n", kts, p);

	do {
		/* 
		 * call the corresponding driver poll fn, 
		 * wait for 500ms, arbitrary delay
		 */ 
		rc = (de->ktlid_fns->ktli_dfns_poll)(dh, 500);
		printf("BE Poll returned: %d\n", rc);
	
		/* -1 error, 0 timeout, 1 need to receive data */
		if (rc < 0) {
			printf("%s:%d: poll failed %d\n",
			       __FILE__, __LINE__, errno);
			perror("Poll:");
			continue;
		}
		if (!rc )
			continue;
#if 0		
		/* wait for work */
		pthread_mutex_lock(&rq->ktq_m);
		if (!rq->ktq_exit) {
			pthread_cond_wait(&rq->ktq_cv, &rq->ktq_m);
		}
		pthread_mutex_unlock(&rq->ktq_m);
 		printf("Receiver: caught signal\n");
#endif

		/* PAK: need a kio timeout mechanism 
		 * Maybe after 30 or 60 500ms polls */

		
		ktli_recvmsg(kts);
#if 0
		/* if the receive q is not empty, get active */
		while (list_size(rq->ktq_list)) {
			if (rq->ktq_exit) break;
			printf("Received Message\n");
		}
#endif
		if (rq->ktq_exit) break;
	
	} while (1);
	
	printf("Receiver: exiting\n");

	pthread_exit(p);
}

