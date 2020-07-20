#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>

#include "kinetic.h"
#include "getlog.h"
#include "message.h"
#include "session.h"

#include "ktli.h"

int64_t ki_getaseq(struct kiovec *msg, int msgcnt);
void    ki_setseq(struct kiovec *msg, int msgcnt, int64_t seq);
static int32_t ki_msglen(struct kiovec *msg_hdr);

/* Kinetic API helpers */
static struct ktli_helpers ki_kh = {
	.kh_recvhdr_len = KP_LENGTH,
	.kh_getaseq_fn	= ki_getaseq,
	.kh_setseq_fn	= ki_setseq,
	.kh_msglen_fn	= ki_msglen,
};

static int32_t
ki_msglen (struct kiovec *msg_hdr)
{
	kpdu_t *pdu;

	if (!msg_hdr || (msg_hdr->kiov_len !=  KP_LENGTH)) {
		return(-1);
	}

	pdu = (kpdu_t *)msg_hdr->kiov_base;
	return (pdu->kp_msglen + pdu->kp_vallen);
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
 * This code will also issue getlog limits and get version RPCs to get the 
 * connection ID, the cluster version, and to setup the session limits. 
 *
 * A kinetic command header and limits structure is filled out and 
 * preserved on the session config. It can then be referenced by each 
 * future RPC call. 
 *
 * 
 */
int
ki_open(char *host, char *port, uint32_t usetls, int64_t id, char *hmac)
{
	int ktd, rc;
	struct ktli_config *cf;
	ksession_t *ks;
	kgetlog_t glog;
	kgltype_t glt;
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
	memset(&glog, 0, sizeof(kgetlog_t));
	glt = KGLT_LIMITS;
	glog.kgl_type = &glt;
	glog.kgl_typecnt = 1;

	kstatus = ki_getlog(ktd, &glog);
	memcpy(&ks->ks_l, &glog.kgl_limits, sizeof(klimits_t));
	
	/* PAK: Getversion call() to set session cluster version */
	
	return(ktd);
}

int
ki_close(int ktd)
{
	

}
