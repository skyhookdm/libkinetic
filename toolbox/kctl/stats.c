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
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <math.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

static void print_kop(kopstat_t *kop, char *opstr, struct kargs *ka);

#define CMD_USAGE(_ka) kctl_stats_usage(_ka)

void 
kctl_stats_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-T           Enable time statistics\n");
	fprintf(stderr, "\t-d           Print Delete Statistics\n");
	fprintf(stderr, "\t-g           Print Get Statistics\n");
	fprintf(stderr, "\t-p           Print Put Statistics\n");
	fprintf(stderr, "\t-n           Print Noop/Ping Statistics\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

/**
 *  Issue either the batchstart or batchend command
 */
int
kctl_stats(int argc, char *argv[], int kts, struct kargs *ka)
{
	extern char     *optarg;
        extern int	optind, opterr, optopt;
	int 		gets, puts, dels, noops;
	int		tstats, clear;
        char		c;
	kstats_t 	*kst;
	kstatus_t 	krc;
	
	/* clear global flag vars */
	tstats = 0, clear = 0;
	gets = puts = dels = noops = 0;
	
        while ((c = getopt(argc, argv, "CgpdnT?h")) != EOF) {
                switch (c) {
		case 'C':
		        clear = 1;
			break;
			
		case 'd':
			dels = 1;
			break;
			
		case 'g':
			gets = 1;
			break;
			
		case 'n':
			noops = 1;
			break;
			
		case 'p':
			puts = 1;
			break;
			
		case 'T':
			tstats = 1;
			break;
			
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }
	
	/* Shouldn't be any other args */
	if (argc - optind) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	if (!(kst = ki_create(kts, KSTATS_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	if (clear) {
		printf("Clearing Statistics\n");
		krc = ki_putstats(kts, kst);
		if (krc != K_OK) {
			printf("Failed to enable Time Statistics\n");
			return(-1);
		}

		ki_destroy(kst);
		return(0);
	}

	if (tstats) {
		/* Turns on Time stamps and clears stats */
		if (puts) 
			KIOP_SET(&kst->kst_puts, KOPF_TSTAT);
		if (gets)
			KIOP_SET(&kst->kst_gets, KOPF_TSTAT);
		if (dels)
			KIOP_SET(&kst->kst_dels, KOPF_TSTAT);
		if (noops)
			KIOP_SET(&kst->kst_noops, KOPF_TSTAT);
#if 0
		KIOP_SET(&kst->kst_cbats, KOPF_TSTAT);
		KIOP_SET(&kst->kst_sbats, KOPF_TSTAT);
		KIOP_SET(&kst->kst_bputs, KOPF_TSTAT);
		KIOP_SET(&kst->kst_bdels, KOPF_TSTAT);
		KIOP_SET(&kst->kst_range, KOPF_TSTAT);
		KIOP_SET(&kst->kst_getlog, KOPF_TSTAT);
#endif
		if (!ka->ka_quiet)
			printf("Enabling Time Statistics\n");
		
		krc = ki_putstats(kts, kst);
		if (krc != K_OK) {
			printf("Failed to enable Time Statistics\n");
			return(-1);
		}

		ki_destroy(kst);
		return(0);
	}

	krc = ki_getstats(kts, kst);
	if (krc != K_OK) {
		printf("Statistics failed: %s\n", ki_error(krc));
		return(-1);
	}

	if (noops) {
		print_kop(&kst->kst_noops, "Noop/Ping", ka);
	}
	
	if (dels) {
		print_kop(&kst->kst_dels, "Delete", ka);
	}
	
	if (gets) {
		print_kop(&kst->kst_gets, "Get", ka);
	}
	
	if (puts) {
		print_kop(&kst->kst_puts, "Put", ka);
	}

	ki_destroy(kst);
	return(0);
}

static void
print_kop(kopstat_t *kop, char *optstr, struct kargs *ka)
{
	printf("%s RPC statistics\n", optstr);
	printf("\tSuccessful: %7u (%3.4g%%)\n", kop->kop_ok,
	       ((kop->kop_ok == 0)? (double)0.0 : (double)
	        kop->kop_ok /
		(kop->kop_ok + kop->kop_err + kop->kop_dropped) * 100.0));
	printf("\t    Failed: %7u (%3.4g%%)\n", kop->kop_err,
	       ((kop->kop_ok == 0)? (double)0.0 : (double)
	        kop->kop_err /
		(kop->kop_ok + kop->kop_err + kop->kop_dropped) * 100.0));
	printf("\t   Dropped: %7u (%3.4g%%)\n", kop->kop_dropped,
	       ((kop->kop_ok == 0)? (double)0.0 : (double)
	        kop->kop_dropped /
		(kop->kop_ok + kop->kop_err + kop->kop_dropped) * 100.0));
	printf("\n");

	printf("\tSend size, mean: %10.010g B (stddev=%g)\n", kop->kop_ssize, kop->kop_sstdev);
	printf("\tRecv size, mean: %10.010g B\n", kop->kop_rsize);
	printf("\tKey len,   mean: %10.010g B\n", kop->kop_klen);
	printf("\tValue len, mean: %10.010g B\n", kop->kop_vlen);
	printf("\n");
	
	if (KIOP_ISSET(kop, KOPF_TSTAT)) {
		printf("\tRPC time,  mean: %10.010g \xC2\xB5S (stddev=%g)\n",
		       kop->kop_tot[KOP_TMEAN],
		       kop->kop_tot[KOP_TSTDDEV]);
		printf("\tReq time,  mean: %10.010g \xC2\xB5S (stddev=%g)\n",
		       kop->kop_req[KOP_TMEAN],
		       kop->kop_req[KOP_TSTDDEV]);
		printf("\tResp time, mean: %10.010g \xC2\xB5S (stddev=%g)\n",
		       kop->kop_resp[KOP_TMEAN],
		       kop->kop_resp[KOP_TSTDDEV]);
	}

#ifdef TRECORDS
	if (IOP_ISSET(kop, KOPF_TRECORDS)) {
		int fd, i;
		char fname[80];
		pid_t pid = getpid();
		sprintf(fname, "%s.%s.%d", ka->ka_progname, optstr, pid);
		printf("Open Filename: %s\n", fname);
		       fd = open(fname, (O_CREAT|O_RDWR));
		if (fd < 0) {
			perror("File");
			break;
		}
		dprintf(fd, "tt,st,rt\n");
		for (i=1; i<=kop->kop_ok; i++)
			dprintf(fd, "%lu,%lu,%lu\n",
				kop->kop_times[i][KOP_TT],
				kop->kop_times[i][KOP_ST],
				kop->kop_times[i][KOP_RT]);
		close(fd);
		break;
	} 
#endif
	return;
}

