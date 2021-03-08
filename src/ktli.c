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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>

#include "ktli.h"
#include "ktli_session.h"

/*
 * KTLI - Kinetic Transport Layer Interface
 */

#define KTLI_POLLINTERVAL (10 * 1000) /* 10 uS, needed locally only  */

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
		sq->ktq_list = list_create();
		pthread_mutex_init(&sq->ktq_m, NULL);
		pthread_cond_init(&sq->ktq_cv, NULL);
	}

	if (rq) {
		rq->ktq_list = list_create();
		pthread_mutex_init(&rq->ktq_m, NULL);
		pthread_cond_init(&rq->ktq_cv, NULL);
	}

	if (cq) {
		cq->ktq_list = list_create();
		pthread_mutex_init(&cq->ktq_m, NULL);
		pthread_cond_init(&cq->ktq_cv, NULL );
	}

	if (!kts || !sq || !rq || !cq ||
	    !sq->ktq_list || !rq->ktq_list || !cq->ktq_list) {
		/* undo any successful allocations */
		(void)(sq->ktq_list?
		       list_destroy(sq->ktq_list, (void *)LIST_NODEALLOC):0);
		(void)(rq->ktq_list?
		       list_destroy(rq->ktq_list, (void *)LIST_NODEALLOC):0);
		(void)(cq->ktq_list?
		       list_destroy(cq->ktq_list, (void *)LIST_NODEALLOC):0);
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
	list_destroy(sq->ktq_list, (void *)LIST_NODEALLOC);
	list_destroy(rq->ktq_list, (void *)LIST_NODEALLOC);
	list_destroy(cq->ktq_list, (void *)LIST_NODEALLOC);
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
	debug_printf("Sender: %p\n",res);

	/* free the sender list and queue */
	/* Pull any kio's off the list and free them */
	/* PAK : this is unecessary code */
	while (list_size(q->ktq_list)) {
		KTLI_FREE(list_remove_rear(q->ktq_list));
	}

	/* free the list and the queue */
	list_destroy(q->ktq_list, (void *)LIST_NODEALLOC);
	KTLI_FREE(q);

	/* close down the receiver thread */
	q = kts_recvq(kts);
	tid = kts_receiver(kts);

	pthread_mutex_lock(&q->ktq_m);
	q->ktq_exit = 1;
	pthread_cond_signal(&q->ktq_cv);
	pthread_mutex_unlock(&q->ktq_m);
	pthread_join(tid, &res);
	debug_printf("Receiver: %p\n",res);

	/* free the receiver list and queue*/
	/* Pull any kio's off the list and free them */
	/* PAK: unecessary code */
	while (list_size(q->ktq_list)) {
		KTLI_FREE(list_remove_rear(q->ktq_list));
	}

	/* free the list and the queue */
	list_destroy(q->ktq_list, (void *)LIST_NODEALLOC);
	KTLI_FREE(q);

	/* Free up the completion queue */
	q = kts_compq(kts);

	/* Signal ktli_polls to exit */
	q->ktq_exit = 1; 

	/* Pull any kio's off the list and free them */
	/* PAK: Unecessary code */
	while (list_size(q->ktq_list)) {
		KTLI_FREE(list_remove_rear(q->ktq_list));
	}

	/* pause to allow any miscreant ktli_polls to exit */
	usleep((KTLI_POLLINTERVAL * 5));

	/* free the list and the queue */
	list_destroy(q->ktq_list, (void *)LIST_NODEALLOC);
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
	struct ktli_queue *rq;
	struct ktli_config *cf;

	if (!kts_isvalid(kts)) {
		errno = EBADF;
		return(-1);
	}

	dh = kts_dhandle(kts);
	de = kts_driver(kts);
	st = kts_state(kts);
	cf = kts_config(kts);
	rq = kts_recvq(kts);

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

	/*
	 * Receiver thread was launched at open and is waiting
	 * for the connectioN. Wake it up to start the receiver service.
	 */
	pthread_mutex_lock(&rq->ktq_m);
	pthread_cond_signal(&rq->ktq_cv);
	pthread_mutex_unlock(&rq->ktq_m);

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
	kio->kio_errno = 0;
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
	if (!list_insert_after(sq->ktq_list, &kio, sizeof(struct kio *))) {
		errno = ENOMEM;
		pthread_mutex_unlock(&sq->ktq_m);
		return(-1);
	}

	/* preserve the Q back pointer  */
	kio->kio_qbp = list_element_curr(sq->ktq_list);
	kio->kio_state = KIO_NEW;

	/* wake up the sender */
	pthread_cond_signal(&sq->ktq_cv);

	pthread_mutex_unlock(&sq->ktq_m);

	return(0);
}

/*
 * List helper function to find a matching kio given a seq number
 * If no match return LIST_TRUE to continue searching
 * Once a match is found return FALSE to terminate search 
 */
static list_boolean_t
ktli_kiomatch(void *data, void *ldata)
{
	list_boolean_t match = LIST_TRUE;
	struct kio *kio = (struct kio *)data;
	struct kio *lkio = *(struct kio **)ldata;

	if (kio == lkio)
		match = LIST_FALSE;  /* See comment above */
	return (match);
}

/*
 * List helper function to find a kio with no req sendmsg
 * If no match return LIST_TRUE to continue searching
 * Once a match is found return FALSE to terminate search 
 */
static list_boolean_t
ktli_kionoreq(void *data, void *ldata)
{
	list_boolean_t match = LIST_TRUE;
	struct kio *lkio = *(struct kio **)ldata;

	if (lkio->kio_sendmsg.km_cnt == 0) /* sendmsg vector - no req */
		match = LIST_FALSE;  /* See comment above */
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
 * Error means that the KIO was never found or possibly doesn't exist
 * Success means that a KIO was located and removed from KTLI's control.
 */
int
ktli_receive(int kts, struct kio *kio)
{
	int rc = 0;
	enum ktli_sstate st;
	struct ktli_queue *cq;
	struct kio **lkio;

	errno = 0;
	
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

	/*
	 * if the KIO has been received then the KIO has a back pointer
	 * into the CQ. So set the CQ curr to that back pointer and pop
	 * it off the list.
	 */
	if (kio->kio_state == KIO_RECEIVED) {
		/*
		 * check for a NULL as a safety check that the back pointer
		 * is valid for the CQ
		 */
		if (list_setcurr(cq->ktq_list, kio->kio_qbp)) {
			lkio = (struct kio **)list_remove_curr(cq->ktq_list);
			assert(kio == *lkio);
			KTLI_FREE(lkio);

			kio->kio_qbp = NULL; /* no longer on a q */

			/* 
			 * Could be receiving a KIO in any state:
			 *	KIO_RECEIVED
			 *	KIO_FAILED
			 *	KIO_TIMEOUT
			 * That is still a successful receive of a valid KIO. 
			 * Caller can use KIO state to determine what to do.
			 */
			errno = kio->kio_errno;
			rc = 0;

		} else {
			/* Getting here is a bug */
			assert(0);
		}
	} else {
		errno = ENOENT;
		debug_printf("Attempted receveive on a non ready KIO\n");
		rc = -1;
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
	rc = list_traverse(cq->ktq_list, kio, ktli_kionoreq, LIST_ALTR);
	if (rc == LIST_EXTENT || rc == LIST_EMPTY) {
		errno = ENOENT;
		rc = -1;
	} else {
		/* Found the requested kio */
		lkio = (struct kio **)list_remove_curr(cq->ktq_list);
		*kio = *lkio;
		KTLI_FREE(lkio);
		rc = 0;

		(*kio)->kio_qbp = NULL;  /* No longer on a Q */
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
 * becomes ready, a timeout occurs or until the session is disconnected.
 *
 * @param kts A connected kinetic session descriptor.
 * @param timeout Number of micro seconds to wait, approximate
 *
 */
int
ktli_poll(int kts, int timeout)
{
	enum ktli_sstate st;
	struct ktli_queue *cq;
	int timeleft;
	struct timespec interval, remain;

	errno = 0;
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

	interval.tv_sec		= 0;
	interval.tv_nsec	= KTLI_POLLINTERVAL;
	remain.tv_sec 		= 0;
	remain.tv_nsec		= 0;
	timeleft		= timeout * 1000;	/* uS -> nS */

	do {
		nanosleep(&interval, &remain);

		pthread_mutex_lock(&cq->ktq_m);

		/* Check the for completed items */
		if (list_size(cq->ktq_list)) {
			pthread_mutex_unlock(&cq->ktq_m);
			return(0);
		}

		/* see if someone pulled the rug out from under us */
		if (cq->ktq_exit) {
			pthread_mutex_unlock(&cq->ktq_m);
			errno = ECONNABORTED;
			return(-1);
		}
		
		pthread_mutex_unlock(&cq->ktq_m);

		/* Update the timeleft, if caller passed a timeout */
		if (timeout) {
			timeleft =- (interval.tv_nsec - remain.tv_nsec);
			if (timeleft <= 0) {
				errno = ETIMEDOUT;
				return(-1);
			}
		}
	} while(1);
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

			(*kio)->kio_qbp = NULL; /* No longer on a Q */
			(*kio)->kio_state = KIO_FAILED;

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
 * int ktli_drain_match(int kts, struct kio *kio)
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
	uint32_t tqlen; /* sum of all q sizes */
	enum ktli_sstate st;
	struct kio **lkio;
	struct ktli_queue *q;

	errno = 0;
	
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
		rc = list_traverse(q->ktq_list, kio, ktli_kiomatch, LIST_ALTR);

		if (rc != LIST_EXTENT && rc != LIST_EMPTY) {
			/* Found the requested kio */
			lkio = (struct kio **) list_remove_curr(q->ktq_list);
			assert(kio == *lkio);
			KTLI_FREE(lkio);
			rc = 0;

			kio->kio_qbp = NULL; /* No longer on a Q */
			kio->kio_state = KIO_FAILED;
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
 * int ktli_config (int kts, struct ktli_config *cf)
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
	enum kio_state state;	 	/* Temp state var */

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

	debug_printf("Sender: starting %d (%p)\n", kts, p);

	/* Main processing loop, continue until told to leave */
	do {
		/* Wait for an event */
		pthread_mutex_lock(&sq->ktq_m);

		/*
		 * a kio could be added after the while loop below terminated
		 * and this point. Also the exit flag could have
		 * been raised. So only if the send queue is empty and no
		 * exit is signalled, should we wait
		 */
		if (!list_size(sq->ktq_list) && !sq->ktq_exit)
			/* Empty Q, need to wait. */
			pthread_cond_wait(&sq->ktq_cv, &sq->ktq_m);

		pthread_mutex_unlock(&sq->ktq_m);

		debug_printf("Sender: caught signal\n");

		if (sq->ktq_exit) break;

		/* Process the send queue */
		while (list_size(sq->ktq_list)) {

			pthread_mutex_lock(&sq->ktq_m);
			lkio = (struct kio **)list_remove_front(sq->ktq_list);
			kio = *lkio;
			KTLI_FREE(lkio);

			/* no longer on a Q, clear the Q back pointer  */
			kio->kio_qbp = NULL;

			pthread_mutex_unlock(&sq->ktq_m);

			/* Use current session seq for this kio */
		        kio->kio_seq = kts_sequence(kts);

			/* bump the session seq for the next message */
			kts_set_sequence(kts, kio->kio_seq + 1);

			(kh->kh_setseq_fn)(kio->kio_sendmsg.km_msg,
					     kio->kio_sendmsg.km_cnt,
					     kio->kio_seq);

			/*
			 * PREEMPIVELY Q
			 * If a response is needed, pre-emptively place
			 * this KIO on the rq to avoid a race with the
			 * receiver. Of course if there is an error I will
			 * have to dequeue it before moving the failed kio
			 * to the cq.
			 */
			if (!KIOF_ISSET(kio, KIOF_REQONLY)) {
				pthread_mutex_lock(&rq->ktq_m);
				(void)list_mvrear(rq->ktq_list);
				list_insert_after(rq->ktq_list,
						   &kio, sizeof(struct kio *));
				/* preserve the Q back pointer  */
				kio->kio_qbp = list_element_curr(rq->ktq_list);
				pthread_mutex_unlock(&rq->ktq_m);
			}


			/* Set timeout time */
			clock_gettime(KIO_CLOCK, &kio->kio_timeout);

			/* Incrment the current time to be the timeout time */
			kio->kio_timeout.tv_sec += KIO_TIMEOUT_S;

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
			/*
			 * Although on the rq, setting the state outside of
			 * rq lock is probably OK as the receiver code
			 * only looks for its existence on the rq to match
			 * with inbound KIO.  The SENT state is really for
			 * debugging and completeness.
			 */
			kio->kio_state = KIO_SENT;
			debug_printf("ktli: Sent Kio: %p: Seq %ld\n",
				     kio, kio->kio_seq);

			/* Handle the REQONLY case with the error case */
			if (rc < 0 || KIOF_ISSET(kio, KIOF_REQONLY)) {
				/*
				 * Tortured logic here.
				 * Three possible KIOs can get here:
				 *    1. Failed REQUEST
				 *    2. Failed REQONLY
				 *    3. Successful REQONLY
				 * First handle both Failed REQUEST & REQONLY
				 */
				if (rc < 0) {
					debug_printf("KTLI Send Error\n");
					state = KIO_FAILED;
				}

				/*
				 * Since REQONLY are not queued on rq
				 * pop the Failed REQUEST kio off the rq.
				 */
				if (!KIOF_ISSET(kio, KIOF_REQONLY)) {
					/*
					 * Why is it on the rq? See
					 * PREEMPIVELY Q comment above.
					 */
					pthread_mutex_lock(&rq->ktq_m);
					(void)list_setcurr(rq->ktq_list,
							   kio->kio_qbp);
					lkio = (struct kio **)list_remove_curr(rq->ktq_list);
					KTLI_FREE(lkio);

					/* no longer on a Q,
					   clear the Q back pointer  */
					kio->kio_qbp = NULL;
					
					/* leave the list ready for an insert */
					(void)list_mvrear(rq->ktq_list);
					pthread_mutex_unlock(&rq->ktq_m);
				} else if (rc >= 0) {
					/* This is the Successful REQONLY */
					state = KIO_RECEIVED;
				}

				/*
				 * In any case: error REQONLY/REQRESP or
				 * ok REQONLY, hang it on the completed Q
				 */
				pthread_mutex_lock(&cq->ktq_m);
				(void)list_mvrear(cq->ktq_list);
				list_insert_after(cq->ktq_list,
						  &kio, sizeof(struct kio *));

				/* preserve the Q back pointer  */
				kio->kio_qbp = list_element_curr(cq->ktq_list);
				kio->kio_state = state;

				pthread_cond_broadcast(&cq->ktq_cv);
				pthread_mutex_unlock(&cq->ktq_m);

				continue;
			}

			/* Success  signal the receiver */
			pthread_mutex_lock(&rq->ktq_m);
			pthread_cond_signal(&rq->ktq_cv);
			pthread_mutex_unlock(&rq->ktq_m);
		}

	} while (1); /* forever */

	debug_printf("Sender: exiting\n");

	pthread_exit(p);
}

/*
 * List helper function to find a kio that should be timed out
 * If no match, return LIST_TRUE to continue searching
 * Once a match is found return FALSE to terminate search
 */
static list_boolean_t
ktli_timechk(void *data, void *ldata)
{
	list_boolean_t match = LIST_TRUE;
	struct timespec *currtime = (struct timespec *)data;
	struct kio *lkio = *(struct kio **)ldata;

	/*
	 * If current time secs is greater than
	 * KIO timeout secs we have a KIO with exhausted
	 * wait time
	 */
	if (currtime->tv_sec > lkio->kio_timeout.tv_sec) {
		match = LIST_FALSE;  /* See comment above */
	}
	return (match);
}

/*
 * List helper function to find a matching kio given a seq number
 * If no match, return LIST_TRUE to continue searching
 * Once a match is found return FALSE to terminate search
 */
static list_boolean_t
ktli_seqmatch(void *data, void *ldata)
{
	list_boolean_t match = LIST_TRUE;
	int64_t *aseq = (int64_t *)data;
	struct kio *lkio = *(struct kio **)ldata;

	if (*aseq == lkio->kio_seq)
		match = LIST_FALSE;  /* See comment above */
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
 * Need to handle EAGAIN, EWOULDBLOCK, EINTR
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
	 * Need to allocate a kiovec array,
	 * one kiovec for the PDU[0], one kiovec for the MSG[1]
	 * and another for the potential VAL[2], so 3 elements. 
	 */
	msg.km_msg = KTLI_MALLOC(sizeof(struct kiovec) * KM_CNT_WITHVAL);
	if (!msg.km_msg) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_fprintf(stderr, "%s:%d: KTLI_MALLOC failed\n",
			      __FILE__, __LINE__);
		goto recvmsgerr;
	}

	/* Allocate the header buffer */
	msg.km_msg[KIOV_PDU].kiov_base = KTLI_MALLOC(kh->kh_recvhdr_len);
	if (!msg.km_msg[KIOV_PDU].kiov_base) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_fprintf(stderr, "%s:%d: KTLI_MALLOC failed\n",
			      __FILE__, __LINE__);
		goto recvmsgerr;
	}
	msg.km_msg[KIOV_PDU].kiov_len = kh->kh_recvhdr_len;

	/* call the corresponding driver receive to receive the PDU */
	rc = (de->ktlid_fns->ktli_dfns_receive)(dh, &msg.km_msg[KIOV_PDU], 1);
	if (rc == -1) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_fprintf(stderr, "%s:%d: receive failed %d\n",
			      __FILE__, __LINE__, rc);
		goto recvmsgerr;
	}

	/* With the PDU, call helper fn to get the msg and val lengths */
	msg.km_msg[KIOV_MSG].kiov_len =	(kh->kh_msglen_fn)(&msg.km_msg[KIOV_PDU]);
	msg.km_msg[KIOV_VAL].kiov_len =	(kh->kh_vallen_fn)(&msg.km_msg[KIOV_PDU]);

	if ((msg.km_msg[KIOV_MSG].kiov_len < 0) ||
	    (msg.km_msg[KIOV_VAL].kiov_len < 0)) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_printf("%s:%d: Helper msglen failed failed\n",
		       __FILE__, __LINE__);
		goto recvmsgerr;
	}

	/* Allocate the message and value buffers buffer */
	msg.km_msg[KIOV_MSG].kiov_base = KTLI_MALLOC(msg.km_msg[KIOV_MSG].kiov_len);
	if (!msg.km_msg[KIOV_MSG].kiov_base) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_fprintf(stderr, "%s:%d: KTLI_MALLOC failed\n",
			      __FILE__, __LINE__);
		goto recvmsgerr;
	}

	msg.km_msg[KIOV_VAL].kiov_base = KTLI_MALLOC(msg.km_msg[KIOV_VAL].kiov_len);
	if (!msg.km_msg[KIOV_VAL].kiov_base) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_fprintf(stderr, "%s:%d: KTLI_MALLOC failed\n",
			      __FILE__, __LINE__);
		goto recvmsgerr;
	}

	/* call the corresponding driver receive to get the message */
	rc = (de->ktlid_fns->ktli_dfns_receive)(dh, &msg.km_msg[KIOV_MSG], 2);
	if (rc == -1) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_printf("%s:%d: receive failed %d\n",
			     __FILE__, __LINE__, errno);
		perror("");
		goto recvmsgerr;
		assert(0);
	}

	/* We have the message. Find its matching request */
	aseq = (kh->kh_getaseq_fn)(msg.km_msg, KM_CNT_WITHVAL);
	debug_printf("KTLI Received ASeq: %ld\n", aseq);

	/* search through the recvq, need the mutex */
	pthread_mutex_lock(&rq->ktq_m);
	rc = list_traverse(rq->ktq_list, &aseq, ktli_seqmatch, LIST_ALTR);

	if (rc == LIST_EXTENT || rc == LIST_EMPTY) {
		/*
		 * No matching kio.  Allocate a kio, set it up and mark it as
		 * received. Let the upper layers deal with it.
		 */
		debug_printf("KTLI Received Unsolicited\n");
		kio = KTLI_MALLOC(sizeof(struct kio));
		if (!kio) {
			debug_fprintf(stderr, "%s:%d: KTLI_MALLOC failed\n",
			       __FILE__, __LINE__);

			pthread_mutex_unlock(&rq->ktq_m);
			goto recvmsgerr;
		}
		memset((void *)kio, 0, sizeof(struct kio));
		kio->kio_seq = aseq;
		KIOF_SET(kio, KIOF_RESPONLY);
	} else {
		debug_printf("KTLI Received Matched KIO\n");
		lkio = (struct kio **) list_remove_curr(rq->ktq_list);
		kio  = *lkio;
		KTLI_FREE(lkio);

		/* Not on a Q, clear the back pointer */
		kio->kio_qbp = NULL;
	}

	(void)list_mvrear(rq->ktq_list); /* reset to rear after traverse */
	pthread_mutex_unlock(&rq->ktq_m);

	if (!kio) {
		/* PAK: HANDLE - Yikes, errors down here suck */
		debug_printf("%s:%d: traverse failed\n", __FILE__, __LINE__);
		assert(0);
		return(-1);
	}

	/* hang response onto the kio */
	kio->kio_recvmsg.km_status = 0;
	kio->kio_recvmsg.km_errno = 0;
	kio->kio_recvmsg.km_cnt = KM_CNT_WITHVAL;
	kio->kio_recvmsg.km_msg = msg.km_msg;

	/* Add to completion queue */
	pthread_mutex_lock(&cq->ktq_m);
	(void)list_mvrear(cq->ktq_list);
	list_insert_after(cq->ktq_list, &kio, sizeof(struct kio *));

	/* preserve the Q back pointer  */
	kio->kio_qbp = list_element_curr(cq->ktq_list);
	kio->kio_state = KIO_RECEIVED;
	assert(kio->kio_qbp);

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

		/* Not on a Q, clear the back pointer */
		kio->kio_qbp = NULL;

		kio->kio_state = KIO_FAILED;
		kio->kio_errno = ECONNABORTED;
		list_insert_after(cq->ktq_list, &kio, sizeof(struct kio *));

		/* preserve the Q back pointer  */
		kio->kio_qbp = list_element_curr(cq->ktq_list);
	}

	while (list_size(sq->ktq_list)) {
		lkio = (struct kio **)list_remove_front(sq->ktq_list);
		kio = *lkio;
		KTLI_FREE(lkio); /* created by the list */

		/* Not on a Q, clear the back pointer */
		kio->kio_qbp = NULL;

		kio->kio_state = KIO_FAILED;
		kio->kio_errno = ECONNABORTED;
		list_insert_after(cq->ktq_list, &kio, sizeof(struct kio *));

		/* preserve the Q back pointer  */
		kio->kio_qbp = list_element_curr(cq->ktq_list);
	}

	/* notify anyone sleeping on the completion queue */
	pthread_cond_broadcast(&cq->ktq_cv);
	pthread_mutex_unlock(&cq->ktq_m);
	pthread_mutex_unlock(&rq->ktq_m);
	pthread_mutex_unlock(&sq->ktq_m);

	/* free any buffers allocated */
	if (msg.km_msg[KIOV_PDU].kiov_base)
		KTLI_FREE(msg.km_msg[KIOV_PDU].kiov_base);
	if (msg.km_msg[KIOV_MSG].kiov_base)
		KTLI_FREE(msg.km_msg[KIOV_MSG].kiov_base);
	if (msg.km_msg[KIOV_VAL].kiov_base)
		KTLI_FREE(msg.km_msg[KIOV_VAL].kiov_base);
	if (msg.km_msg)
		KTLI_FREE(msg.km_msg);

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
	enum ktli_sstate st;
	struct kio *kio, **lkio;
	struct timespec currtime;
	struct timespec lastcheck = {0,0};

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

	/*
	 * wait for the connection to start
	 * should be signalled by ktli_connect
	 */
	do {

#if 0
		// See issue #33
		pthread_mutex_lock(&rq->ktq_m);
		pthread_cond_wait(&rq->ktq_cv, &rq->ktq_m);
		pthread_mutex_unlock(&rq->ktq_m);
#else
		usleep(100);
#endif
		/* verify kts is connected */
		st = kts_state(kts);
	} while (st != KTLI_SSTATE_CONNECTED);

	debug_printf("Receiver: starting %d (%p)\n", kts, p);

	/* main forever loop */
	do {
		/*
		 * call the corresponding driver poll fn,
		 * wait for 10ms, arbitrary delay
		 */
		rc = (de->ktlid_fns->ktli_dfns_poll)(dh, 10);
		//debug_printf("Receiver: BE Poll returned: %d\n", rc);

		/* -1 error, 0 timeout, 1 need to receive data */
		if (rc < 0) {
			/* PAK: FIX, infinite loop when errno = ECONNABORTED */
			debug_printf("%s:%d: poll failed %d\n",
				     __FILE__, __LINE__, errno);
			perror("Poll:");
			continue;
		}

		if (rc == 1) {
			/* Must be something waiting */
			debug_printf("calling ktli_recvmsg\n");
			ktli_recvmsg(kts);
		}

		/*
		 * Poll timeouts (rc == 0) fall through,
		 * completing the time out check
		 */

		/*
		 * KIO timeout check code:
		 * Check the rq for KIOs that need to be timed out.
		 */
		pthread_mutex_lock(&rq->ktq_m);

		/* Get the current clock, vdso(7) makes this fast */
		clock_gettime(KIO_CLOCK, &currtime);

		/*
		 * Timeouts are done on a second granularity,
		 * If the last checks seconds equal current seconds
		 * no need to check
		 */
		if (lastcheck.tv_sec == currtime.tv_sec) {
			goto skip_timeout_check;
		}

		/*
		 * Traverse the Q to see if anything should be timed out
		 * Traverse defaults to starting at the front. Use LIST_ALTR
		 * so that curr points to matched KIO if any.
		 */
		rc = list_traverse(rq->ktq_list, &currtime,
				   ktli_timechk, LIST_ALTR);
		while (rc == LIST_OK) {
			if (rc != LIST_EXTENT && rc != LIST_EMPTY) {
				/* \
				 * Found one KIO to timeout. Pull it off the
				 * receive Q and mark it timedout
				 */
				lkio = (struct kio **)list_remove_curr(rq->ktq_list);
				kio = *lkio;
				KTLI_FREE(lkio);  /* created by the list */
				kio->kio_errno = ETIMEDOUT;

				/* Not on a Q, clear the back pointer */
				kio->kio_qbp = NULL;

				debug_printf("KIO Timeout seq: %ld\n",
					     kio->kio_seq);

				/*
				 * Add the found KIO to the completed Q.
				 * Remember we have the recev Q locks,
				 * This is the correct lock order sq, rq, cq
				 */
				pthread_mutex_lock(&cq->ktq_m);
				(void)list_mvrear(cq->ktq_list);

				list_insert_after(cq->ktq_list, &kio,
						  sizeof(struct kio *));

				/* preserve the Q back pointer  */
				kio->kio_qbp = list_element_curr(cq->ktq_list);
				kio->kio_state = KIO_TIMEOUT;

				pthread_cond_broadcast(&cq->ktq_cv);
				pthread_mutex_unlock(&cq->ktq_m);

				/* Continue the search at the curr list ptr */
				rc = list_traverse(rq->ktq_list,
						   &currtime, ktli_timechk,
						   (LIST_CURR|LIST_ALTR));
			}
		}

		/* Finished a check, set the last check var */
		lastcheck = currtime;

	skip_timeout_check:
		/* KIO timeout check finished, reset and release the rq */
		(void)list_mvrear(rq->ktq_list);
		pthread_mutex_unlock(&rq->ktq_m);

		if (rq->ktq_exit) break;

	} while (1);

	debug_printf("Receiver: exiting\n");

	pthread_exit(p);
}

