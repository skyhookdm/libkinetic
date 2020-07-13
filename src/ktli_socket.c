/* 
 * KTLI Socket Driver 
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ktli.h"

static void * ktli_socket_open();
static int ktli_socket_close(void *dh);
static int ktli_socket_connect(void *dh, char *host, char *port, int usetls);
static int ktli_socket_disconnect(void *dh);
static int ktli_socket_send(void *dh, struct kiovec *msg, int msgcnt);
static int ktli_socket_receive(void *dh, struct kiovec *msg, int msgcnt);
static int ktli_socket_poll(void *dh, int timeout);

struct ktli_driver_fns socket_fns = {
	.ktli_dfns_open		= ktli_socket_open,
	.ktli_dfns_close	= ktli_socket_close,
	.ktli_dfns_connect	= ktli_socket_connect,
	.ktli_dfns_disconnect	= ktli_socket_disconnect,
	.ktli_dfns_send		= ktli_socket_send,
	.ktli_dfns_receive	= ktli_socket_receive,
	.ktli_dfns_poll		= ktli_socket_poll,
};

static void *
ktli_socket_open()
{
	int *dd;

	dd = malloc(sizeof(int));
	if (!dd) {
		errno = ENOMEM;
		return(NULL);
	}
	
	/* 
	 * This is just a place holder file descriptor, real work is done 
	 * in ktli_socket_connect() and dup-ed to this descriptor.
	 * ipv4 and ipv6 are both supported.
	 */
	*dd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	return((void *)dd);
}

static int
ktli_socket_close(void *dh)
{
	int dd;
	
	if (!dh) {
		errno -EINVAL;
		return(-1);
	}
	dd = *(int *)dh;

	/* Ignoring close errs, could end up with a descriptor leak */
	close(dd);
	free(dh);
	
	return (0);
}

int
ktli_socket_connect(void *dh, char *host, char *port, int usetls)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int dd, sfd, rc, on, MiB, flags;

	if (!dh || !host || !port) {
		errno -EINVAL;
		return(-1);
	}
	dd = *(int *)dh;

	/* for now TLS not supported, may be another driver, maybe not */
	if (usetls) {
		errno = EINVAL;
		return(-1);
	}

	/* Obtain address(es) matching host/port */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Stream socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          /* Any protocol */

	rc = getaddrinfo(host, port, &hints, &result);
	if (rc != 0) {
		return(-1);
	}

	/* 
	 * getaddrinfo() returns a list of address structures.
	 * Try each address until we successfully connect(2).
	 * If socket(2) (or connect(2)) fails, we (close the socket
	 * and) try the next address. 
	 */
	for (rp = result; rp != NULL; rp = rp->ai_next) {
               sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
               if (sfd == -1)
                   continue;

               if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
                   break;                  /* Success */

               close(sfd);
           }

           freeaddrinfo(result);           /* No longer needed */

           if (rp == NULL) {
		   /* No address succeeded - errno set in last connect failure */
		   return(-1);
           }

	   /*
	    * Increase send/recv buffers to at least 1MiB. Most servers 
	    * support this but the real number comes from a GETLOG Limits call
	    * and can be refined later
	    */
	   MiB = 1024 * 1024;
	   rc = setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &MiB, sizeof(MiB));
	   if (rc == -1) {
		   fprintf(stderr, "Error setting socket send buffer size\n"); 
	   }
	   rc = setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &MiB, sizeof(MiB));
	   if (rc == -1) {
		   fprintf(stderr, "Error setting socket recv buffer size\n"); 
	   }

	   on = 1;
	   setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

#if !defined(__APPLE__)
	   /* 
	    * TCP_CORK is NOT available on OSX
	    * Not sure this is needed as we should be writing a complete
	    * message with a single writev call.  But we will see...
	    * might want to enable disable around the writev
	    */
	   setsockopt(sfd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on));
#endif

#if 0
NONBLOCK OFF
	   flags = fcntl(sfd, F_GETFL, 0);
	   if (flags == -1) {
		   return(-1);
	   }
	   if (fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		   return(-1);
	   }
#endif
	   
	   /* Dup over sfd to placeholder descriptor dd */
	   if (dup2(sfd, dd) < 0) {
		   return(-1);
	   }

	   return(0);
}

int
ktli_socket_disconnect(void *dh)
{
	int rc, dd;

	if (!dh) {
		errno -EINVAL;
		return(-1);
	}
	dd = *(int *)dh;
	
	rc = shutdown(dd, SHUT_RDWR);
	return(rc);
}

int
ktli_socket_send(void *dh, struct kiovec *msg, int msgcnt)
{
	struct iovec *iov;
	int i, len, dd, bw;

	if (!dh) {
		errno -EINVAL;
		return(-1);
	}
	dd = *(int *)dh;
	
	/* Convert the kiov to a std iov for writev */
	iov = (struct iovec *)malloc(sizeof(struct iovec) * msgcnt);
	if (!iov) {
		errno = ENOMEM;
		return(-1);
	}

	for (len=0,i=0; i<msgcnt; i++) {
		iov[i].iov_base = msg[i].kiov_base;
		iov[i].iov_len = msg[i].kiov_len;
		len += msg[i].kiov_len;
	}

	bw = writev(dd, iov, msgcnt);
	if (bw < 0) {
		/* Return the error */
		return(bw);
	}
	
	if (bw != len) {
		errno = ECOMM;
		return(-1);
	}

	free(iov);
	
	return(bw);
}

/*
 * Receive a message into a pre-allocated kiovec array.
 */
int
ktli_socket_receive(void *dh, struct kiovec *msg, int msgcnt)
{
	struct iovec iov[2];
	int i, len, dd, br;

	if (!dh || !msgcnt ) {
		errno -EINVAL;
		return(-1);
	}	
	dd = *(int *)dh;

	/* 
	 * PAK: Need to handle msgcnt > 1, by using readv instead 
	 * would need to create normal iovec, until we do, assert
	 */
	assert(msgcnt==1);
	
	/* do the read */
	br = read(dd, msg[0].kiov_base, msg[0].kiov_len);
	if (br < 0) {
		/* Return the error */
		return(br);
	}

	/* PAK: need to loop continuing to read if br < kiov_len */
	if (br !=  msg[0].kiov_len) {
		errno = ECOMM;
		return(-1);
	}
	
	return(br);

}

int
ktli_socket_poll(void *dh, int timeout)
{
	int rc;
	struct pollfd pfd;
	
	if (!dh) {
		errno -EINVAL;
		return(-1);
	}
	
	pfd.fd = *(int *)dh;
	pfd.events = POLLIN;

	rc = poll(&pfd, 1, timeout);

	/* Badness occurred */
	if (rc < 0) {
		/* errno already set */
		return(-1);
	}

	/* received a hangup */
	if (rc && pfd.revents & POLLHUP) {
		errno = ECONNABORTED;
		return(-1);
	}
	
	/* Data Waiting */
	if (rc && pfd.revents & POLLIN)
		return(1);

	/* some event occurred but not one we were looking for */
	if (rc) {
		errno = ENOMSG;
		return(-1);
	}
	
	/* Timed out */
	return(0);
	
}

