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
#include <errno.h>
#include <math.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"

int s_stat_updatekop(kopstat_t *kop);


/* Access and utility routines for Kinetic Stats */
kstatus_t
ki_getstats(int ktd, kstats_t *kst)
{
	int rc;
	ksession_t *ses;		/* KTLI Session info */
	struct ktli_config *cf;		/* KTLI configuration info */

	if (!kst) {
		debug_printf("stat: bad param");
		return(K_EINVAL);
	}
		
	/* Get KTLI config, Kinetic session and Kinetic stats structure */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("stat: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;
	*kst = ses->ks_stats;

	/* Finish the Sample Var and Stddev calculations */
	s_stat_updatekop(&kst->kst_puts);
	s_stat_updatekop(&kst->kst_gets);
	s_stat_updatekop(&kst->kst_dels);
	
	return(K_OK);
}


kstatus_t
ki_putstats(int ktd, kstats_t *kst)
{
	int rc;
	ksession_t *ses;		/* KTLI Session info */
	struct ktli_config *cf;		/* KTLI configuration info */

	if (!kst) {
		debug_printf("stat: bad param");
		return(K_EINVAL);
	}
		
	/* Get KTLI config, Kinetic session and Kinetic stats structure */
	rc = ktli_config(ktd, &cf);
	if (rc < 0) {
		debug_printf("stat: ktli config");
		return(K_EBADSESS);
	}
	ses = (ksession_t *) cf->kcfg_pconf;

	ses->ks_stats = *kst;
	return(K_OK);
}


/* 
 * Time stamp subtraction to get an interval in microseconds
 * 	_me is the minuend and is a timespec structure ptr
 * 	_se is the subtrahend and is a timespec structure ptr
 * 	_d is the difference in microseconds, should be a uint64_t
 *	KOP_BNS is 1B nanoseconds 
 */
#define KOP_BNS 1000000000L
#define KOP_MAXINTV 1000000 /* 1M uS = 1S */
#define ts_sub(_me, _se, _d) {						\
	(_d)  = ((_me)->tv_nsec - (_se)->tv_nsec);			\
	if ((_d) < 0) {							\
		--(_me)->tv_sec;					\
		(_d) += KOP_BNS;					\
	}								\
	(_d) += ((_me)->tv_sec - (_se)->tv_sec) * KOP_BNS;		\
	(_d) /= (uint64_t)1000;						\
	if ((_d) > KOP_MAXINTV || (_d) < 0) {				\
	/*	printf("KCTL TS CHK: (%lu, %lu) - (%lu, %lu) = %lu\n",	\
		       (_me)->tv_sec, (_me)->tv_nsec,			\
		       (_se)->tv_sec, (_se)->tv_nsec,			\
		       (_d));	*/					\
		(_d) = 0;						\
	}								\
}


/*
 * The next set of macros accurately calculate a running sample variance,
 * almost.  They calculate the Sums needed to calulate the variance.
 * Why not calulate the variance and stddev? Well this routine is in
 * IO path so minimizing the math helps a little. Variance and stddev
 * are easy calculations from these sums once a user asks for the 
 * statistics structure.
 * The algorithm goes back to a 1962 paper by B. P. Welford and is 
 * presented in Donald Knuth’s Art of Computer Programming, Vol 2, 
 * page 232, 3rd edition.
 * https://www.johndcook.com/blog/standard_deviation/
 */
#define tsa_first(_tsa, _t)						\
	(_tsa)[KOP_TTOTAL]  = (double)(_t);				\
	(_tsa)[KOP_TMEAN]   = (double)(_t);				\
	(_tsa)[KOP_TMEANSQ] = (double)0.0;				\
	
#define tsa_next(_tsa, _t, _n) {					\
	double nmn, nsq;						\
	(_tsa)[KOP_TTOTAL]  += (double)(_t);				\
	nmn = (_tsa)[KOP_TMEAN]   + ((_t) - (_tsa)[KOP_TMEAN]) / (_n);	\
	nsq = (_tsa)[KOP_TMEANSQ] + ((_t) - (_tsa)[KOP_TMEAN]) * ((_t) - nmn);\
	(_tsa)[KOP_TMEAN]   = nmn;					\
	(_tsa)[KOP_TMEANSQ] = nsq;					\
}


/*
 * Add a timestamp data point to the running time stamp statistics
 */
int
s_stats_addts(struct kopstat *kop, struct kio *kio)
{
	int64_t st, rt, tt;  /* sent, receive, total time in nS */

	if (!kop || !kio) {
		return(-1);
	}

        /* The times here are always in temporal order and generally close */
	ts_sub(&kio->kio_ts.kiot_comp, &kio->kio_ts.kiot_start, tt);
	ts_sub(&kio->kio_ts.kiot_sent, &kio->kio_ts.kiot_start, st);
	ts_sub(&kio->kio_ts.kiot_comp, &kio->kio_ts.kiot_recvs, rt);
	if (!tt || !st || !rt) {
		//printf("Dropping TS (%lu,%lu,%lu)\n", tt,st,rt);
		kop->kop_ok--;
		kop->kop_dropped++;
		return(-1);
	}
		
#if 0
	printf("Start: %lu, %lu\n",
	       kio->kio_ts.kiot_start.tv_sec, kio->kio_ts.kiot_start.tv_nsec);
	printf("Sent: %lu, %lu\n",
	       kio->kio_ts.kiot_sent.tv_sec, kio->kio_ts.kiot_sent.tv_nsec);
	printf("RecvS: %lu, %lu\n",
	       kio->kio_ts.kiot_recvs.tv_sec, kio->kio_ts.kiot_recvs.tv_nsec);
	printf("Comp: %lu, %lu\n",
	       kio->kio_ts.kiot_comp.tv_sec, kio->kio_ts.kiot_comp.tv_nsec);
	if (kop->kop_ok == 1) 
		printf("tt, st, rt \n");
	printf("%lu, %lu, %lu\n", tt, st, rt);
#endif
	kop->kop_times[kop->kop_ok][KOP_TT] = tt;
	kop->kop_times[kop->kop_ok][KOP_ST] = st;
	kop->kop_times[kop->kop_ok][KOP_RT] = rt;
	
	/* Now calculate the vaious sums needed */
	if (kop->kop_ok == 1) {
		tsa_first(kop->kop_tot,  tt);
		tsa_first(kop->kop_req,  st);
		tsa_first(kop->kop_resp, rt);
	} else {
		tsa_next(kop->kop_tot,  tt, kop->kop_ok);
		tsa_next(kop->kop_req,  st, kop->kop_ok);
		tsa_next(kop->kop_resp, rt, kop->kop_ok);
	}

	return(0);
}


/**
 * Update KOP elements that depend on the dynamic data
 */
int
s_stat_updatekop(kopstat_t *kop)
{
	double var;
	
	if (!KIOP_ISSET(kop, KOPF_TSTAT)) {
		return(0);
	}

	var = kop->kop_smsq/(kop->kop_ok-1);
	kop->kop_sstdev = sqrt(var);
	
	/* Calculate the Sample Variance and StdDev */
	kop->kop_tot[KOP_TVAR] = kop->kop_tot[KOP_TMEANSQ]/(kop->kop_ok-1);
	kop->kop_tot[KOP_TSTDDEV] = sqrt(kop->kop_tot[KOP_TVAR]);
	
	kop->kop_req[KOP_TVAR] = kop->kop_req[KOP_TMEANSQ]/(kop->kop_ok-1);
	kop->kop_req[KOP_TSTDDEV] = sqrt(kop->kop_req[KOP_TVAR]);
	
	kop->kop_resp[KOP_TVAR] = kop->kop_resp[KOP_TMEANSQ]/(kop->kop_ok-1);
	kop->kop_resp[KOP_TSTDDEV] = sqrt(kop->kop_resp[KOP_TVAR]);

	return(0);
}
