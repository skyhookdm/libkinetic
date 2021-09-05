/**
 * Copyright 2020-2021 Seagate Technology LLC.
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 * https://mozilla.org/MP:/2.0/.
 *
 * This program is distributed in the hope that it will be useful,
 * but is provided AS-IS, WITHOUT ANY WARRANTY; iX1ncluding without
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
 * License for more details.
 *
 */

/*
 * KTLI Socket Driver
 */
#define _GNU_SOURCE         /* See feature_test_macros(7) */
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
#include <sys/uio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

//#define KTLI_ZEROCOPY 1

#include "kinetic.h"
#include "ktli.h"

static void * ktli_socket_open();
static int ktli_socket_close(void *dh);
static int ktli_socket_connect(void *dh, char *host, char *port, int usetls);
static int ktli_socket_disconnect(void *dh);
static int ktli_socket_send(void *dh, struct kiovec *msg, int msgcnt);
static int ktli_socket_receive(void *dh, struct kiovec *msg, int msgcnt);
static int ktli_socket_poll(void *dh, int timeout);

typedef struct device_handle {
	int	dh_dd;		/* Socket descriptor for non TLS conns */
	char 	*dh_host;	/* Preserve host for logging */
	char	*dh_port;	/* Preserve port for logging */
	SSL_CTX *dh_ctx;	/* TLS conn context */
	SSL 	*dh_ssl;	/* TLS conn descriptor */
} dhandle_t;

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
	dhandle_t *dh;

	/*
	 * Just setup the device handle and go,
	 * real work is in ktli_socket_connect()
	 */
	dh = calloc(1, sizeof(dhandle_t));
	if (!dh) {
		errno = ENOMEM;
		return(NULL);
	}

	/* Since 0 is a valid file descriptor init dd to -1 */
	dh->dh_dd = -1;

	return((void *)dh);
}


static int
ktli_socket_close(void *vdh)
{
	dhandle_t *dh = (dhandle_t *)vdh;

	if (!dh) {
		errno = EINVAL;
		return(-1);
	}

	/* Just in case disconnect was not called */
        /* Ignoring close errs, could end up with a descriptor leak */
	if (dh->dh_dd > -1)	close(dh->dh_dd);
	if (dh->dh_host)	free(dh->dh_host);
	if (dh->dh_port)	free(dh->dh_port);
	if (dh->dh_ssl)		SSL_free(dh->dh_ssl);
	if (dh->dh_ctx)		SSL_CTX_free(dh->dh_ctx);
	free(dh);

	return (0);
}


/*
 * Connect the SSL portion of the connection, if requested
 */
static int
ktli_socket_connectSSL(dhandle_t *dh)
{
	int err, rc;

	do {
		SSL_set_fd(dh->dh_ssl, dh->dh_dd);
		rc = SSL_connect(dh->dh_ssl);
		if (rc == 1) {
			return(0);
		}

		err = SSL_get_error(dh->dh_ssl, rc);

		if (err == SSL_ERROR_WANT_READ ||
		    err == SSL_ERROR_WANT_WRITE) {
			/* Max 1 second between retries */
			ktli_socket_poll(dh, 1000);

		} else {
			printf("SSL Connect: error\n");
			errno = ENOPROTOOPT;
			return(-1);
		}
	} while (1);
}


int
ktli_socket_connect(void *vdh, char *host, char *port, int usetls)
{
	dhandle_t *dh = (dhandle_t *)vdh;
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, rc, on = 1, MiB, flags;

	if (!dh || !host || !port) {
		errno = EINVAL;
		return (-1);
	}

	dh->dh_host = strdup(host);
	dh->dh_port = strdup(port);

	if (usetls) {
		SSL_library_init();
		OpenSSL_add_all_algorithms();
		dh->dh_ctx = SSL_CTX_new(SSLv23_client_method());
		dh->dh_ssl = SSL_new(dh->dh_ctx);
		if(!dh->dh_ctx || !dh->dh_ssl) {
			fprintf(stderr, "Error setting up TLS: %s\n",
				ERR_lib_error_string(ERR_peek_last_error()));
			errno = ENOPROTOOPT;
			return(-1);
		}
		SSL_set_mode(dh->dh_ssl, SSL_MODE_AUTO_RETRY);
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

#ifdef KTLI_ZEROCOPY
	       /*
		* On some systems this must be done before
		* the socket is connected
		*/
	       /* printf("ZeroCopy: Enabled\n"); */
	       if (setsockopt(sfd, SOL_SOCKET, SO_ZEROCOPY, &on, sizeof(on)))
		       perror("setsockopt zerocopy failed");
#else
	       /* printf("ZeroCopy: Disabled\n");*/
#endif

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
	   MiB = 5 * 1024 * 1024;
	   rc = setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &MiB, sizeof(MiB));
	   if (rc == -1) {
		   fprintf(stderr, "Error setting socket send buffer size\n");
	   }
	   rc = setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &MiB, sizeof(MiB));
	   if (rc == -1) {
		   fprintf(stderr, "Error setting socket recv buffer size\n");
	   }

#define KTLI_CORK 0

#if KTLI_CORK && !defined(__APPLE__)
	   /*
	    * TCP_CORK is NOT available on OSX and is TCP_NOPUSH in BSD
	    * so not really portable.
	    *
	    * If set, don't send out partial frames. All queued partial
	    * frames are sent when the option is cleared again. This is
	    * useful for prepending headers before calling sendfile(2),
	    * or for throughput optimization. As currently implemented,
	    * there is a ** 200-millisecond ceiling** on the time for which
	    * output is corked by TCP_CORK. If this ceiling is reached,
	    * then queued data is automatically transmitted."
	    *
	    * Requires a flush but diabling CORK and re-enabling it after
	    * every writev.
	    *
	    * Not sure this is needed  But we will see...

	    printf("Putting in the cork\n");
	   */

	   setsockopt(sfd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on));
#endif

	   /*
	    * NODELAY means that segments are always sent as soon as possible,
	    * even if there is only a small amount of data. When not set,
	    * data is buffered until there is a sufficient amount to send out,
	    * thereby avoiding the frequent sending of small packets, which
	    * results in poor utilization of the network. This option is
	    * overridden by TCP_CORK; however, setting this option forces
	    * an explicit flush of pending output, even if TCP_CORK is
	    * currently set.
	    */
	   if (setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)))
		   printf("setsockopt tcp_nodelay");

	   flags = fcntl(sfd, F_GETFL, 0);
	   if (flags == -1) {
		   return(-1);
	   }
	   if (fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		   return(-1);
	   }

	   /* Save the socket descriptor */
	   dh->dh_dd = sfd;

	   /* If requested connect via SSL */
	   if (dh->dh_ssl) return(ktli_socket_connectSSL(dh));

	   return(0);
}


int
ktli_socket_disconnect(void *vdh)
{
	dhandle_t *dh = (dhandle_t *)vdh;

	if (!dh) {
		errno = EINVAL;
		return(-1);
	}

	/* Socket is created in connect so undo connect here */
	shutdown(dh->dh_dd, SHUT_RDWR);
	if (dh->dh_dd > -1)	close(dh->dh_dd);
	if (dh->dh_host)	free(dh->dh_host);
	if (dh->dh_port)	free(dh->dh_port);
	if (dh->dh_ssl)		SSL_free(dh->dh_ssl);
	if (dh->dh_ctx)		SSL_CTX_free(dh->dh_ctx);

	memset(dh, 0, sizeof(dhandle_t));
	dh->dh_dd = -1;
       
	return(0);
}


static int
ktli_socket_sendSSL(dhandle_t *dh, struct iovec *msg, int msgcnt)
{
	int curv, bw, tbw, cnt, err;

	if (!dh || !msgcnt) {
		errno = -EINVAL;
		return(-1);
	}

	/*
	 * SSL_write can do partial writes, handle em
	 * Loop through the vectors, partial write means there can be
	 * successive loop iterations that are trying to complet one vector
	 * element.  loop ends when all vectors are exhausted
	 * cnt is used for loop stats
	 */
	for (curv=0,tbw=0,cnt=1; curv<msgcnt; cnt++) {

		/* Allow for empty iovs */
		if (!msg[curv].iov_len) {
			curv++;
			continue;
		}
		bw = SSL_write(dh->dh_ssl,
			       msg[curv].iov_base, msg[curv].iov_len);

		if (bw > 0) {
			/* This always continues the loop */
			if (bw == msg[curv].iov_len) {
				/* This vector element is exhausted */
				curv++;
			} else {
				msg[curv].iov_base += bw;
				msg[curv].iov_len  -= bw;
				tbw += bw;

			}
			continue;
		}

		/* Error Cases */
		err = SSL_get_error(dh->dh_ssl, bw);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
			/*
			 * Underlying BIO is not ready for write,
			 * should retry.
			 */
			continue;
		}

		/* This send is toast, bailing */
		printf("ktli_socket_sendSSL: failed: %s\n",
		       ERR_reason_error_string(err));
		perror("ktli_socket_sendSSL: ");
		errno = EIO;
		return(-1);
	}

	return(tbw);
}


int
ktli_socket_send(void *vdh, struct kiovec *msg, int msgcnt)
{
	dhandle_t *dh = (dhandle_t *)vdh;

#ifdef KTLI_ZEROCOPY
	struct msghdr hdr = {NULL, 0, NULL, 0, NULL, 0, 0};
#endif
	struct iovec *iov;
	int i, len, iovs, curv, bw, tbw, cnt;

	if (!dh || !msgcnt) {
		errno = -EINVAL;
		return(-1);
	}


	/*
	 * Copy the kiov to a std iov for use with writev
	 * This temp iov structure also allows the base ptrs to be modified
	 * for partial readswrites, not impacting the user passed in kiov
	 * Take this opportunity to also get a total length
	 */
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
	iovs = msgcnt;

	/* If TLS then sendSSL */
	if (dh->dh_ssl) {
		/* new iov is used so that it can be manipulated in sendSSL */
		bw = ktli_socket_sendSSL(dh, iov, iovs);
		free(iov);
		return(bw);
	}

	/*
	 * NONBLOCKING sockets can do partial writes, handle em
	 * init the current vector and total bytes read,
	 * cnt is used for loop stats
	 * then loop until complete
	 */
	for (curv=0,tbw=0,cnt=1;;cnt++) {
#ifdef KTLI_ZEROCOPY
		hdr.msg_iov = &iov[curv];
		hdr.msg_iovlen = iovs-curv;
		bw = sendmsg(dh->dh_dd, &hdr, MSG_ZEROCOPY);
#else
		bw = writev(dh->dh_dd, &iov[curv], iovs-curv);
#endif
		if (bw <= 0) {
			/*
			 * Although EAGAIN and EWOULDBLOCK are equivalent,
			 * POSIX.1-2001 allows either error to be returned
			 * for this case, and does not require these
			 * constants to have the same value, so check for
			 * both possibilities.
			 * EINTR should also be handled in case the system
			 * call is interupted by a signal
			 */
			if ((errno == EAGAIN)	   ||  	/* Not ready */
			    (errno == EWOULDBLOCK) ||  	/* Not ready */
			    (errno == EINTR)) {		/* Intr by signal */
				bw = 0;
				continue;
			} else {
				/*
				 * Not a retry-able error break out,
				 * we check the error outside of the loop
				 */
				break;
			}
		}

		tbw += bw;

#if KTLI_CORK && !defined(__APPLE__)
		/* Putting this here, so that it's defined when needed,
		 * and not defined (and unused) otherwise.
		 *
		 * If cork is used, need to flush by resetting NODELAY.
		 */
		setsockopt(dh->dh_dd, I
			   PPROTO_TCP, TCP_NODELAY, &on,  sizeof(on));
#endif

		/* run through the io vectors that can be fully consumed */
		while ((curv < iovs) && (bw >= iov[curv].iov_len))
			/* consume bw and inc curv */
			bw -= iov[curv++].iov_len;

		/* are we done? */
		if (curv == iovs)
			break;

		/* partial vector update the current io vectors base and len */
		iov[curv].iov_base = (char *)iov[curv].iov_base + bw;
		iov[curv].iov_len -= bw;

	}

	if (bw < 0) {
		/*
		 * if bw < 0 then writev failed above, return the error
		 */
		printf("socket_send: error %d", errno);
		perror("socket_send:");
		free(iov);
		return(-1);
	}

	if (tbw != len) {
		/* This should catch the EOF error */
		printf("socket_send: %d != %d \n", tbw, len);
		errno = ECOMM;
		free(iov);
		return(-1);
	}

	free(iov);
	return(tbw);
}


static int
ktli_socket_receiveSSL(dhandle_t *dh, struct iovec *msg, int msgcnt)
{
	int curv, br, tbr, cnt, err;

	if (!dh || !msgcnt) {
		errno = -EINVAL;
		return(-1);
	}

	/*
	 * SSL_write can do partial writes, handle em
	 * Loop through the vectors, partial write means there can be
	 * successive loop iterations that are trying to complet one vector
	 * element.  loop ends when all vectors are exhausted
	 * cnt is used for loop stats
	 */
	for (curv=0,tbr=0,cnt=1; curv<msgcnt; cnt++) {

		/* Allow for empty iovs */
		if (!msg[curv].iov_len) {
			curv++;
			continue;
		}

		br = SSL_read(dh->dh_ssl,
			       msg[curv].iov_base, msg[curv].iov_len);

		if (br > 0) {
			/* This always continues the loop */
			if (br == msg[curv].iov_len) {
				/* This vector element is full */
				curv++;
			} else {
				msg[curv].iov_base += br;
				msg[curv].iov_len  -= br;
				tbr += br;

			}
			continue;
		}

		/* Error Cases */
		err = SSL_get_error(dh->dh_ssl, br);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
			/*
			 * Underlying BIO is not ready for write,
			 * should retry.
			 */
			continue;
		}

		/* This send is toast, bailing */
		printf("ktli_socket_receiveSSL: failed: %s\n",
		       ERR_reason_error_string(err));
		perror("ktli_socket_receiveSSL: ");
		errno = EIO;
		return(-1);
	}

	return(tbr);
}


/*
 * Receive a message into a pre-allocated kiovec array.
 */
int
ktli_socket_receive(void *vdh, struct kiovec *msg, int msgcnt)
{
	dhandle_t *dh = (dhandle_t *)vdh;
	struct iovec *iov;
	int i, len, iovs, curv, br, tbr, cnt;

	if (!dh || !msgcnt) {
		errno = -EINVAL;
		return(-1);
	}

	/*
	 * Copy the kiov to a std iov for use with readv
	 * This temp iov structure also allows the base ptrs to be modified
	 * for partial reads, not impacting the user passed in kiov
	 * Take this opportunity to also get a total length
	 */
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
	iovs = msgcnt;

	/* If TLS then receiveSSL */
	if (dh->dh_ssl) {
		/* new iov is used so that it can be manipulated in sendSSL */
		br = ktli_socket_receiveSSL(dh, iov, iovs);
		free(iov);
		return(br);
	}

	/*
	 * NONBLOCKING sockets can do partial reads, handle em
	 * init the current vector and total bytes read,
	 * then loop until complete
	 */
	for (curv=0,tbr=0,cnt=1;;cnt++) {
		br = readv(dh->dh_dd, &iov[curv], iovs-curv);
		if (br <= 0) {
			/*
			 * Although EAGAIN and EWOULDBLOCK are equivalent,
			 * POSIX.1-2001 allows either error to be returned
			 * for this case, and does not require these
			 * constants to have the same value, so check for
			 * both possibilities.
			 * EINTR should also be handled in case the system
			 * call is interupted by a signal
			 */
			if ((errno == EAGAIN)	   ||  	/* Not ready */
			    (errno == EWOULDBLOCK) ||  	/* Not ready */
			    (errno == EINTR)) {		/* Intr by signal */
				br = 0;
				continue;
			} else {
				/*
				 * we check the error outside of the loop
				 * br < 0, errno has the error,
				 * br = 0 is EOF, Socket has been closed
				 */
				break;
			}
		}

		tbr += br;

		/* run through the io vectors that can be fully consumed */
		while ((curv < iovs) && (br >= iov[curv].iov_len))
			/* consume br and inc curv */
			br -= iov[curv++].iov_len;

		/* are we done? */
		if (curv == iovs)
			break;

		/* partial vactor update the current io vectors base and len */
		iov[curv].iov_base = (char *)iov[curv].iov_base + br;
		iov[curv].iov_len -= br;
	}

	if (br < 0) {
		/*
		 * if br < 0 then readv failed above, return the error
		 */
		printf("socket_receive: error %d", errno);
		free(iov);
		return(-1);
	}

	if (tbr != len) {
		/* This should catch the EOF error */
		printf("socket_receive: %d != %d \n", tbr, len);
		errno = ECOMM;
		free(iov);
		return(-1);
	}

	free(iov);
	return(tbr);
}

int
ktli_socket_poll(void *vdh, int timeout)
{
	int rc;
	dhandle_t *dh = (dhandle_t *)vdh;
	struct pollfd pfd;

	if (!dh) {
		errno = -EINVAL;
		return(-1);
	}

	pfd.fd = dh->dh_dd;
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
#ifdef KTLI_ZEROCOPY
#else
	if (rc) {
		errno = ENOMSG;
		return(-1);
	}
#endif
	/* Timed out */
	return(0);

}

