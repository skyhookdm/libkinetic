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

/* 
 * ***** Kinetic Transport Layer Interface Session Table
 * This module is to abstract the management of the global KTLI sessions
 * table.  It could be completely abstracted into generic table data
 * structure module, but at the moment that is unwarranted. For now 
 * the table contents are completely tied to KTLI.  The point of the 
 * session table is to hand back to a caller a descriptor
 * that respresents a unique session.  A table is used to make lookups
 * simply a dereference.  The anticipated number of concurrent sessions 
 * is small enough that a preallocated table makes sense. 
 * The session stores:
 *	o a pointer to the backend driver for the session
 * 	o a backend driver 
 *	o the recv and send queues
 * 
 * Using a static table with compile time max but could easily 
 * provide an interface to grow or shrink the number of supported sessions.
 * Table rows are small, a handful pf pointers, so 1k sessions <= 50k data.
 * Table allocations can occur in parallel by multithreaded programs, so
 * care must be taken when allocating table slots. 
 */
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "ktli.h"
#include "ktli_session.h"

#define KTS_MAX_SESSIONS 1024
enum kts_queues {
	KTS_SENDQ = 0,
	KTS_RECVQ = 1,
	KTS_COMPQ = 2,
	KTS_MAXQ = 3		/* Used for allocations */
};

enum kts_threads {
	KTS_SENDTID = 0,
	KTS_RECVTID = 1,
	KTS_MAXTIDS = 2		/* Used for allocations */
};

struct kts_session {
	struct ktli_driver	*kts_driver;	/* Ptr to the driver fns */
	void 			*kts_dhandle;	/* Driver provided handle */
	struct ktli_helpers 	*kts_helpers;	/* Helper fns and data */
	struct ktli_queue	*kts_queue[KTS_MAXQ];  
	pthread_t 		kts_pthread[KTS_MAXTIDS];
	enum ktli_sstate	kts_state;	/* Session state */
	int64_t			kts_sequence;	/* Next sequence # */
	struct ktli_config 	*kts_config;	/* Session configuration*/
};

static int kts_table_size = KTS_MAX_SESSIONS;
static struct kts_session *kts_table[KTS_MAX_SESSIONS];

/*
 * TODO: not defined, but maybe should be?
static int kts_set_driver_atomic(int, struct ktli_driver *,
				 struct ktli_driver *);
*/

void
kts_init()
{
	memset(kts_table, 0,
	       (KTS_MAX_SESSIONS * sizeof(struct kts_session *)));
}

int
kts_alloc_slot()
{
	int i;
	int kts;
	struct kts_session *ks;

	ks = (struct kts_session *)malloc(sizeof(struct kts_session));
	if (!ks) {
		errno = ENOMEM;
		return(-1);
	}
	
	/* 
	 * the KTS table is a shared resource and allocations can occur
	 * in parallel. So we must atomically allocate a session slot. 
	 * Calling kts_set_driver on each slot using NULL as the old value
	 * permits searching and allocating in a single call. Since a NULL 
	 * driver value indicates an available slot, the set driver call 
	 * will fail if the slot is in use and succeed atomically if
	 * available. 
	 */
	kts = -1;
	for(i=0; i<kts_table_size; i++) {
		if (SBCAS(&(kts_table[i]), NULL, ks)) {
			/* we got it, break out */
			kts = i;
			break;
		}
	}
	
	if (kts == -1)
		free(ks);
	
	return(kts);  /* either a valid slot or -1 */
 }
	
void
kts_free_slot(int kts)
{
	struct kts_session *ks;

	ks = kts_table[kts];
	if (SBCAS(&(kts_table[kts]), ks, NULL)) {
		if (kts_table[kts]) free(kts_table[kts]);
	}
}

/* clears should only be done by a single call so no need to protect */
void
kts_clear(int kts)
{
	int i;

	if (!kts_table[kts]) return;

	kts_table[kts]->kts_driver = NULL;
	kts_table[kts]->kts_dhandle = NULL;
	kts_table[kts]->kts_helpers = NULL;
	for(i=0; i<KTS_MAXQ; i++) kts_table[kts]->kts_queue[i] = NULL;
	for(i=0; i<KTS_MAXTIDS; i++) kts_table[kts]->kts_pthread[i] = 0;
	kts_table[kts]->kts_state = KTLI_SSTATE_UNKNOWN;
	kts_table[kts]->kts_sequence = 0;
	kts_table[kts]->kts_config = NULL;
	
	
}

int
kts_isvalid(int kts)
{
	return(!((kts < 0) || (kts > KTS_MAX_SESSIONS) || !kts_table[kts]));
}

void
kts_set(int kts,
	struct ktli_driver *driver,
	void *dhandle,
	struct ktli_helpers *helpers,
	struct ktli_queue *sendq,
	struct ktli_queue *recvq,
	struct ktli_queue *compq,
	pthread_t sender,
	pthread_t receiver,
	enum ktli_sstate state,
	uint64_t sequence,
	struct ktli_config *cf)
{
	if (!kts_table[kts]) return;
	
	kts_table[kts]->kts_driver  = driver;
	kts_table[kts]->kts_dhandle = dhandle;
	kts_table[kts]->kts_helpers = helpers;
	kts_table[kts]->kts_queue[KTS_SENDQ] = sendq;
	kts_table[kts]->kts_queue[KTS_RECVQ] = recvq;
	kts_table[kts]->kts_queue[KTS_COMPQ] = compq;
	kts_table[kts]->kts_pthread[KTS_SENDTID] = sender;
	kts_table[kts]->kts_pthread[KTS_RECVTID] = receiver;
	kts_table[kts]->kts_state = state;
	kts_table[kts]->kts_sequence = sequence;
	kts_table[kts]->kts_config = cf;
}

void
kts_set_driver(int kts, struct ktli_driver *driver)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_driver = driver;
}

void
kts_set_dhandle(int kts, void *dhandle)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_dhandle = dhandle;
}

void
kts_set_helpers(int kts, struct ktli_helpers *helpers)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_helpers = helpers;
}

void
kts_set_sendq(int kts, struct ktli_queue *sendq)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_queue[KTS_SENDQ] = sendq;
}

void
kts_set_recvq(int kts, struct ktli_queue *recvq)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_queue[KTS_RECVQ] = recvq;
}

void
kts_set_compq(int kts, struct ktli_queue *compq)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_queue[KTS_COMPQ] = compq;
}

void
kts_set_sender(int kts, pthread_t tid)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_pthread[KTS_SENDTID] = tid;
}

void
kts_set_receiver(int kts, pthread_t tid)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_pthread[KTS_RECVTID] = tid;
}

void
kts_set_state(int kts, enum ktli_sstate state)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_state = state;
}

void
kts_set_sequence(int kts, int64_t sequence)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_sequence = sequence;
}

void
kts_set_config(int kts, struct ktli_config *cf)
{
	if (!kts_table[kts]) return;
	kts_table[kts]->kts_config = cf;
}

/*
 *  *** References
 */
struct ktli_driver *
kts_driver(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_driver:NULL));
}

void *
kts_dhandle(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_dhandle:NULL));
}

struct ktli_helpers *
kts_helpers(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_helpers:NULL));
}

struct ktli_queue *
kts_sendq(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_queue[KTS_SENDQ]:NULL));
}

struct ktli_queue *
kts_recvq(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_queue[KTS_RECVQ]:NULL));
}

struct ktli_queue *
kts_compq(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_queue[KTS_COMPQ]:NULL));
}

pthread_t
kts_sender(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_pthread[KTS_SENDQ]:0));
}

pthread_t
kts_receiver(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_pthread[KTS_RECVQ]:0));
}

enum ktli_sstate
kts_state(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_state:KTLI_SSTATE_UNKNOWN));
}

int64_t
kts_sequence(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_sequence:-1));
}

struct ktli_config *
kts_config(int kts)
{
	return((kts_table[kts]?kts_table[kts]->kts_config:NULL));
}

int
kts_max_sessions()
{
	return(kts_table_size);
}
