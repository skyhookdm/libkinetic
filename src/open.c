#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"
#include "getlog.h"
#include "message.h"
#include "session.h"

static int32_t ki_msglen(struct kiovec *msg_hdr);

/* Kinetic API helpers */
static struct ktli_helpers ki_kh = {
	.kh_recvhdr_len = KP_PLENGTH,
	.kh_getaseq_fn	= ki_getaseq,
	.kh_setseq_fn	= ki_setseq,
	.kh_msglen_fn	= ki_msglen,
};

static int32_t
ki_msglen (struct kiovec *msg_hdr)
{
	kpdu_t pdu;

	if (!msg_hdr || (msg_hdr->kiov_len !=  KP_PLENGTH)) {
		return(-1);
	}

	/* pull these off in network byte order then convert to host order */
	UNPACK_PDU(&pdu, (unsigned char *)msg_hdr->kiov_base);
		
	return (pdu.kp_msglen + pdu.kp_vallen);
}

/**
 * ki_open
 * Need to open and connect a session here. 
 * Setup the ktli_config structure. The kinetic protocol also needs a few 
 * more pieces of data preserved to use when setting up each RPC. 
 * First we need the connection ID which will be given by the server 
 * after first RPC. Second is the cluster version. Eventually timeouts,
 * priorty, quanta, quick exit flag need to be preserved and used on each
 * RPC.  
 * As part of the connection establishment, kinetic issues an unsolicited 
 * getlog::limits,configuration response that contains the current cluster 
 * version and connection ID in the command header as well as a limits 
 * structure in the body. This needs to be saved on the session for future 
 * reference. 
 */
int
ki_open(char *host, char *port, uint32_t usetls, int64_t id, char *hmac)
{
	int ktd, rc;
	struct ktli_config *cf;
	struct kio *kio;
	struct kiovec *kiov;
	struct kresult_message kmresp;
	ksession_t *ks;
	kgetlog_t glog;
	kcmdhdr_t cmd_hdr;
	kstatus_t kstatus;
	/*
	 * these ktli and session configs get hung on the ktli session
	 * so need to allocate these structures.
	 */
	cf = malloc(sizeof(struct ktli_config));
	ks = malloc(sizeof(ksession_t));
	if (!cf || !ks) {
	}

	memset(cf, 0, sizeof(struct ktli_config));
	memset(ks, 0, sizeof(ksession_t));

	/* Setup the session config structure */
	cf->kcfg_host = strdup(host);
	cf->kcfg_port = strdup(port);
	cf->kcfg_hmac = strdup(hmac);
	cf->kcfg_flags = KCFF_NOFLAGS;
	if (usetls) cf->kcfg_flags |= KCFF_TLS;

	/* 
	 * Nothing to setup on the command header as yet. But setup some
	 * signals (-1) that will allow lower level code to fillout this 
	 * config when it can. Connection ID is the primary example
	 * and will be done within the getlog call below.
	 */
	ks->ks_ch.kch_clustvers = -1;	/* Cluster Version Number */
	ks->ks_ch.kch_connid = -1;	/* Connection ID */
	ks->ks_ch.kch_timeout = 0;	/* Timeout Period */
	ks->ks_ch.kch_pri = NORMAL;	/* Request Priority */
	ks->ks_ch.kch_quanta = 0;	/* Time Quanta */
	ks->ks_ch.kch_qexit = 0;	/* Boolean: Quick Exit */

	/* Hang it on the KTLI session confg*/
	cf->kcfg_pconf = (void *)ks;

	ktd = ktli_open(KTLI_DRIVER_SOCKET, cf, &ki_kh);
	if (ktd < 0 ) {
		return(-1);
	}

	rc = ktli_connect(ktd);
	if (rc < 0) {
		ktli_close(ktd);
		return(-1);
	}

	/* Get the limits structure and save it on the session */
	/* Wait for the response */
	do {
		printf("Polling\n");
		/* wait for something to come in */
		ktli_poll(ktd, 0);
		
		printf("Done Polling\n");		

		/* Check to see if it our response */
		rc = ktli_receive_unsolicited(ktd, &kio);
		printf("Received\n");
		if (rc < 0)
			if (errno == ENOENT)
				/* Not our response, so try again */
				continue;
			else {
				/* PAK: need to exit, receive failed */
			}
		else
			/* Got our response */
			break;
	} while (1);

	kiov = &kio->kio_recvmsg.km_msg[KIOV_MSG];
	kmresp = unpack_kinetic_message(kiov->kiov_base, kiov->kiov_len);

	if (kmresp.result_code == FAILURE) {
		/* cleanup and return error */
		rc = -1;
		goto oex2;
	}

	memset(&glog, 0, sizeof(kgetlog_t));

	kstatus_t command_status = extract_getlog(&kmresp, &glog);
	if (command_status.ks_code != K_OK) {
		rc = -1;
		goto oex1;
	}
	memcpy(&ks->ks_l, &glog.kgl_limits, sizeof(klimits_t));
	memcpy(&ks->ks_conf, &glog.kgl_conf, sizeof(kconfiguration_t));

	command_status = extract_cmdhdr(&kmresp, &cmd_hdr);
	if (command_status.ks_code != K_OK) {
		rc = -1;
		goto oex1;
	}
	memcpy(&ks->ks_ch, &cmd_hdr, sizeof(kcmdhdr_t));

 oex1:
 oex2:
	

	return(ktd);
}

int
ki_close(int ktd)
{
	

}
