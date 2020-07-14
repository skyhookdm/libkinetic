#ifndef _KTLI_H
#define _KTLI_H
/** 
 * Kinetic Transport Layer Interface - an asynchronous message API. 
 * It's primary function is to send and receive opaque kinetic message strings. 
 * Since there are many mechanisms to send and receive strings to and from a 
 * server this module is architected to support a plurality of backend APIs. 
 * For an example a client may want to use normal linux sockets, DPDK sockets,
 * NIC Card TCP offload, or io_uring. This transport layer interface allows 
 * the client to select which one they want to use. So the KTLI module is
 * split vertically into 2 halves. 
 *			  +-------------------------------------+
 *			  |			  KTLI upper half			|
 *			  +--------+--------+--------+----------+
 *		  | Socket | DPDK	| uring  | NIC Card |
 *		  | Driver | Driver | Driver | Driver	|
 *		  +--------+--------+--------+----------+
 *
 * The top half is the generic handling of the outbound and inbound messages,
 * requests and their responses. It provides a basic API for opening, closing,
 * connecting and disconnecting a session to a server. It further provides
 * APIs for sending requests and then reaping the received responses, which
 * are paired by KTLI to the orginating requests. If there messages still 
 * is KTLI when it is bein disconnected and closed, client will need to 
 * drain these messages before a session can be closed. 
 * 
 * The bottom half or backend driver abstracts the mechanics of sending, 
 * receiving messages and session event detection. This permits the use of
 * different network delivery APIs. Ultimately 
 *
 * The basic calling sequence is that a caller opens a session, connects 
 * that session, then sends, polls on that connected session, receiving 
 * once responses have come in. Eventually the caller will disconnect
 * drain and close the session. 
 *								  +-------+								+---+
 *							 +--->| send  |<---+						|	|
 *							 |	  +-------+    |						|	V
 * +------+    +---------+	 |	  +-------+    |	+------------+	  +-------+
 * | open |--->| connect |---|--->| poll  |<---|--->| disconnect |--->| drain |
 * +------+    +---------+	 |	  +-------+    |	+------------+	  +-------+
 *							 |	  +-------+    |						  |
 *							 +--->| recv  |<---+						  V
 *				  +-------+							  +-------+
 *														  | close |
 *											  +-------+
 *
 * The backend APIs are supported by creating KTLI backend drivers. The backend 
 * ktli driver is specified when opening a session and you receive a kinetic 
 * transport session descriptor. Once a ktd is established it can then be 
 * connected to a server through a synchronous connect call. All sends 
 * and receives are asynchronous. The poll function can be blocking or 
 * nonblocking and responds to receive events and failures. 
 * Poll may behave in three ways:
 *	o Blocking Poll if the backing driver supports. 
 *	o Blocking Poll with timeout if the backing driver supports. 
 *	o And finally a spinning Poll if the backing driver supports.
 *
 * KTLI is abstracted below the kinetic protocol layer to allow for a
 * service that supports multiple network backends and is abstracted away
 * from protocol revisions. However, there are several protocol helper 
 * functions and data that are needed to completely abstract the transport 
 * layer. 
 *	o Kinetic server mandates that request sequences numbers on a given
 *	  connection be increasing. Instead of allowing the other layers 
 *	  to provide the sequence number and constantly sorting outbound 
 *	  send queue and potentially stalling the send queue waiting for a
 *	  sequence number to arrive in the queue, KTLI sequences the 
 *	  requests, with the ordering being the arrivial order in the 
 *	  KTLI send queue.	This requires a helper function to set the 
 *	  sequence number in the request.
 *	o When messages are then received the sequence number is acknowledged
 *	  in a separate ackSequence field.	Since KTLI pairs inbound
 *	  reponses with their outbound requests, a helper function is needed
 *	  get the ackSequence number out of the message so that it can
 *	  search the outstanding sent request for a matching sequence number.
 *	o To successfully receive the message the total length of the 
 *	  message is needed. Since mesages are variable length, the PDU 
 *	  which comes fist in the message specifies that messages length. 
 *	  Two pieces of information are therefore needed to receive a 
 *	  message: the length of the PDU which is static and helper function 
 *	  that takes the PDU and returns the message's total length.
 * KTLI takes a structure with these helper functions and data at open time
 * hanging this structure on the session for use by the send and receive code.
 * 
 */
#include "kio.h"
#include "list.h"

/* 
 * KTLI uses GCC builtin CAS for lockless atomic updates
 * bool __sync_bool_compare_and_swap (type *ptr, type oldval type newval, ...)
 *	 if the current value of *ptr is oldval, then write newval into *ptr. 
 * But does it have to be so long of a function name? Make it smaller
*/
#define SBCAS __sync_bool_compare_and_swap

struct ktli_driver;

struct ktli_driver_fns {
	void * (*ktli_dfns_open)();
	int (*ktli_dfns_close)(void *dh);

	int (*ktli_dfns_connect)(void *dh, char *host, char *port,
							 int usetls, int id, char *hmac);
	int (*ktli_dfns_disconnect)(void *dh);
	
	int (*ktli_dfns_send)(void *dh, struct kiovec *msg, int msgcnt);
	int (*ktli_dfns_receive)(void *dh, struct kiovec *msg, int msgcnt);

	int (*ktli_dfns_poll)(void *dh, int timeout);
};

enum ktli_driver_id {
	KTLI_DRIVER_NONE   = 0,
	KTLI_DRIVER_SOCKET	  ,
	KTLI_DRIVER_DPDK	  ,
	KTLI_DRIVER_URING	  ,
	
	/* ↑↑↑↑↑ add new driver id here */
};

struct ktli_queue {
	LIST			*ktq_list;	/* the queue itself */
	pthread_mutex_t  ktq_m;		/* mutex protecting the queue */
	pthread_cond_t	 ktq_cv;	/* condition variable for waiting */
	int				 ktq_exit;	/* queue exit flag */
};

/* 
 * ktli session state machine				   
 *											   +---+
 *											   |   | send, receive, poll
 *											   |   v
 * +---------+	open>  +--------+  connect	+-----------+
 * | unknown |<------->| opened |---------->| connected |--+
 * +---------+ <close  +--------+			+-----------+  |
 *						 ^		  disconnect	  |		   | server hangup
 *						 |	 +--------------------+		   |  or
 *		   drain,receive |	 |							   | error
 *						 |	 v							   |
 *				   +----------+  disconnect  +---------+  |
 *				   | draining |<-------------| aborted |<-+ 
 *				   +----------+				 +---------+ 
 *					  ^   |
 *					  |   | drain/receive
 *					  +---+
 */
enum ktli_sstate {
	KTLI_SSTATE_UNKNOWN   = 0, 
	KTLI_SSTATE_OPENED	  = 1,
	KTLI_SSTATE_CONNECTED = 2,
	KTLI_SSTATE_ABORTED   = 3,
	KTLI_SSTATE_DRAINING  = 4, 
}; 

/**
 * ktli session helper functions and data.
 * The helper functions and data provide enough session info to abstract the 
 * ktli layer from the the kinetic protocol structure.	To accomplish this a 
 * session must be preconfigured with 3 functions and 1 piece of data:
 *	o a header(PDU) length that informs the minimum receive 
 *	o a function that sets the seqence number in an outbound request
 *	o a function that gets the ackSequence number from an inbound response
 *	o a function, that given a header buffer, returns the expected response 
 *	  length
 */
struct ktli_helpers {
	/* houses min recv necessary to determine full message length */
	int kh_recvhdr_len;			

	/* Extracts the ackSequence from a full message in a single kiovec */
	int64_t (*kh_getaseq_fn)(struct kiovec *msg, int msgcnt);

	/* Sets the sequence in a full message in a single kiovec */
	void	(*kh_setseq_fn)(struct kiovec *msg, int msgcnt, int64_t seq);

	/* Returns total length of a message given a header in one kiovec */
	int32_t (*kh_msglen_fn)(struct kiovec *msg_hdr);
};

/* Exposed API */
int ktli_open(enum ktli_driver_id did, struct ktli_helpers *kh);
int ktli_close(int ktd);
int ktli_connect(int ktd, char *host, char *port,
		 int usetls, int id, char *hmac);
int ktli_disconnect(int ktd);
int ktli_send(int ktd, struct kio *kio);
int ktli_receive(int ktd, struct kio *kio);
int ktli_poll(int ktd, int timeout);
int ktli_drain(int ktd, struct kio **kio);
int ktli_drain_match(int ktd, struct kio *kio);

#endif /* _KTLI_H */
