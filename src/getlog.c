#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>

#include "kio.h"
#include "ktli.h"

static int
gl_validate_req(kgetlog_t *glog)
{
	int i, log;

	errno = EINVAL;  /* assume we will find a problem */

	/* Check the the requested types */
	if (!glog->kgl_type || !glog->kgl_typecnt)
		return(-1);
	log = 0;
	for (i=0; i<glog->kgl_typecnt; i++) {
		switch (glog->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
		case KGLT_TEMPERATURES:
		case KGLT_CAPACITIES:		
		case KGLT_CONFIGURATION:	
		case KGLT_STATISTICS:		
		case KGLT_MESSAGES:		
		case KGLT_LIMITS:		
			/* a good type */
			break;

		case KGLT_LOG:
			/* a good type - LOG has special error checking */
			log = 1;
			break;
			
		default:
			return(-1);
		}
	}

	/* If log expect a logname, if not then there shouldn't be a name */
	if (log) {
		if (glog->kgl_log.kdl_name) {
			int n = strlen(glog->kgl_log.kdl_name);
			if (!n || n > 1024) {
				errno = ERANGE;
				return(-1);
			}
		} else {
			/* Log requested but no name fail with default err */
			return(-1);
		}
	} elese if (glog->kgl_log.kdl_name) {
		/* Shouldn't have a name without a LOG type */
		return (-1);
	}

	/* make sure all other ptrs and cnts are NULL and 0 */
	if (glog->kgl_log.kdl_util || glog->kgl_log.kdl_utilcnt ||
	    glog->kgl_log.kdl_temp || glog->kgl_log.kdl_tempcnt ||
	    glog->kgl_log.kdl_stat || glog->kgl_log.kdl_statcnt ||
	    glog->kgl_log.kdl_msgs || glog->kgl_log.kdl_msgscnt) {
		return(-1);
	}

	errno = 0;
	return(0);
}

static int
gl_validate_resp(kgetlog_t *glog)
{

}

kstatus_t
ki_getlog(int ktd, kgetlog_t *glog)
{
	int rc, n;
	kstatus_t krc;
	struct kio *kio;
	kpdu_t pdu = KP_INIT;
	kmsghdr_t kmh;
	kcmdhdr_t cmh;
	struct kresult_message *kmreq, *kmresp;
	
	/* Validate the passed in glog */
	rc = gl_validate_req(glog);
	if (rc < 0) {
		/* errno set by validate */
		return(-1);
	}

	/* create the kio structure */
	kio = (struct kio *)KI_MALLOC(sizeof(struct kio));
	if (!kio) {
		errno = ENOMEM;
		return(-1);
	}
	memset(kio, 0, sizeof(struct kio));

	/* Alocate the kio vectors */
	kio->kio_sendmsg.km_cnt = 2; /* PDU and protobuf */
	n = sizeof(struct kiovec) * kio->kio_sendmsg.km_cnt;
	kio->kio_sendmsg.km_msg = (struct kiovec *)KI_MALLOC(n);
	if (!kio->kio_sendmsg.km_msg) {
		errno = ENOMEM;
		return(-1);
	}		

	/* Hang the PDU buffer */ 
	kio->kio_cmd = KMT_GETLOG;
	kio->kio_sendmsg.km_msg[0].kiov_base = (void *)&pdu;
	kio->kio_sendmsg.km_msg[0].kiov_len = KP_LENGTH;
	
	/* pack the message */
	/* PAK: fill out kmsghdr and kcmdhdr */
	/* PAK: Need to save conn config on session, for use here */
	memset((void *)&kmh, 0, sizeof(kmh));
	kmh.kmh_atype = KA_HMAC;
	kmh.kmd_id = 1;
	kmh.kmd_hmac = "abcdefgh";

	memset((void *)&kch, 0, sizeof(kch));
	kch.kch_clustvers = 0;
	kch.kch_connid = 0;
	kch.kch_type = KMT_GETLOG;
	
	kmreq = create_getlog_message(&kmh, &kch, glog);

	/* PAK: Error handling */
	rc = pack_getlog_request(kmreq,
				 &(kio->kio_sendmsg.km_msg[1].kiov_base),
				 &(kio->kio_sendmsg.km_msg[1].kiov_len));

	/* Setup the PDU */
	pdu->kp_msglen = kio->kio_sendmsg.km_msg[1].kiov_len;
	pdu->kp_vallen = 0;
	
	/* Send the request */
	ktli_send(ktd, &kio);
	printf ("Sent Kio: %p\n", &kio);

	/* Wait for the response */
	ktli_poll(ktd, 0);
	      
	/* Receive the response */
	/* PAK: need error handling */
	rc = ktli_receive(ktd, &kio);

	kmresp = unpack_getlog_resp(kio->kio_sendmsg.km_msg[1].kiov_base,
				    kio->kio_sendmsg.km_msg[1].kiov_len);
	if (kmresp->result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto glex2;
	}

	kstatus = extract_getlog(kmresp, &glog);
	if (kstatus->ks_code != K_OK) {
		rc = -1;
		goto glex1;
	}

	rc = gl_validate_resp(glog);

	if (rc < 0) {
		/* errno set by validate */
		rc = -1;
		goto glex1;
	}

	/* clean up */
 glex1:
	destroy_command(kmresp);
 glex2:
	destroy_command(kmreq);
	destroy_request(kio->kio_sendmsg.km_msg[1].kiov_base);

	/* sendmsg.km_msg[0] Not allocated, static */
	KI_FREE(kio->kio_recvmsg.km_msg[0].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg[1].kiov_base);
	KI_FREE(kio->kio_recvmsg.km_msg);
	KI_FREE(kio->kio_sendmsg.km_msg);
	KI_FREE(kio);
	
	return(rc);
}
