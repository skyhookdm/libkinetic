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
#ifndef _KIO_H
#define _KIO_H
#include <stdint.h>
#include <time.h>
#include <endian.h>

struct kio;

#include "kinetic.h"
#include "kinetic_internal.h"

enum kio_state {
	KIO_NEW      = 0,
	KIO_SENT	,
	KIO_RECEIVED	,
	KIO_FAILED	,
	KIO_TIMEDOUT	,
};

struct kio_msg {
	struct kiovec *km_msg;  /* Ptr to an ARRAY[] of kiovecs that
				   contain the header (PDU), message
				   and potentially a value or other data */
	int km_cnt;             /* number of kiovecs in km_msg ARRAY */
	int km_status;
	int km_errno;
};

/*
 * KIO flags, a bitmap for controlling the KIO's behavior
 */
enum kio_flags {
	KIOF_INIT	= 0x0000,
	KIOF_REQRESP	= 0x0001,	/* Normal case, an RPC */
	KIOF_REQONLY	= 0x0002,	/* Special case for 1 way req traffic */
					/* mutually exclusive wrt normal case */
	KIOF_RESPONLY	= 0x0004,	/* Unsolicited Repsponses */
	KIOF_TSTAMP	= 0x0008,	/* Enable Time stamp collection */

#define KIOF_SET(_kio, _kiof)	((_kio)->kio_flags |= (_kiof))
#define KIOF_CLR(_kio, _kiof)	((_kio)->kio_flags &= ~(_kiof))
#define KIOF_ISSET(_kio, _kiof)	((_kio)->kio_flags & (_kiof))
};

/*
 * Timeout processing.   Messages first go on the send Q, then after they
 * are sent get moved to the receive Q.  Once on the greceive Q only a
 * matching response can move them to the completed Q.  If the matching
 * response never arrives the req can languish forever on the receive Q. So
 * a timeout mechanism is implemented.  Prior to sending the req, the current
 * time is read, a timeout is calculated into the future by adding
 * KIO_TIMEOUT_S to it. This timeout is set on the KIO.  The receive loop
 * then scans for KIOs who's timeout value is older than the current time. Those
 * KIOs are marked as timedout and moved to the completeion Q.
 *
 * This is the number of seconds to wait for a response
 */
#define KIO_TIMEOUT_S 30
#define KIO_CLOCK CLOCK_MONOTONIC

/* This structure is for collecting timestamps during the KIO lifecycle */
struct kio_tstamps {
	struct timespec	kiot_start;	/* KIO RPC start */
	struct timespec	kiot_sent;	/* KIO Sent completed */
	struct timespec	kiot_recvs;	/* KIO Recv start */
	struct timespec	kiot_comp;	/* KIO RPC completed */
};

/**
 * This is a client lib and the majority of exchanges in kinetic are RPCs,
 * ie. req then resp. (there are unsolicited responses and some some req only
 * messages). So a kio has both send (req) and recv (resp) data structures.
 * Since everything is async at this level, the sender must send in enough
 * info to process the received response.
 *
 * Message Buffers (kio_msg)
 * Send msg kiovec buffers are allocated and filled in by the caller.
 * Receive msg kiovec buffers are allocated by the lower receive layers.
 * All kiovec buffers in the kio structure are the responsibility of the
 * caller to free, including buffers allocated by lower level receive code.
 *
 * Timeout value should be set by the KTLI send processing and periodically
 * checked by the receiver thread.
 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define KIO_MAGIC 0x004f494B		// "KIO\0"
#else
#define KIO_MAGIC 0x494B4F00		// "KIO\0"
#endif

struct kio {
	uint32_t	kio_magic;	/* Identifies a valid KIO */
	uint32_t	kio_cmd;	/* kinetic cmd: get, put, getlog, etc
					   for debugging only */
	int64_t		kio_seq;	/* kinetic sequence */

	uint32_t	kio_flags;	/* Flags modifying KIO behavior */
	enum kio_state	kio_state;	/* Internal status of the KIO */
	int		kio_errno;	/* Errno for entire KIO */

	struct kio_msg	kio_sendmsg;	/* fully allocated and populated
					   msg buffers for send */

	struct kio_msg	kio_recvmsg;	/* passed in empty to be filled
					   by the receive code. Caller
					   responsible to free msg buffers
					   within. */

	struct timespec	kio_timeout;	/* Timestamp when msg should be failed*/

	void 		*kio_qbp;	/* Queue element back pointer */ 

	/* Saved caller params and context for aio */
	void 		*kio_cctx;
	kv_t		*kio_ckv;
        kv_t		*kio_caltkv;
	kb_t		*kio_ckb;
	kapplet_t	*kio_ckapp;

	struct kio_tstamps kio_ts;	/* Time Stamps, used only when enabled
					   via kio_flags */
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
