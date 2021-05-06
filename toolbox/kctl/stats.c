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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <math.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

struct histo {
	uint32_t	h_lower;
	uint32_t	h_upper;
	uint32_t	h_buckets;	/* cols on X axis */
	uint32_t	h_rows;		/* rows on Y axis */
	uint32_t	h_rclip;	/* # rows to clip from top of Y axis */
	uint32_t	*h_histo;
};

static void print_kop(kopstat_t *kop, char *opstr, struct kargs *ka);
static int print_kvs(struct kargs *ka, int kts,
		     char *start, int starti, char *end, int endi, int count,
		     struct histo *hkey, struct histo *hval, int csv);

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
	fprintf(stderr, "\t-N           Print Noop/Ping Statistics\n");
	fprintf(stderr, "\t-c           Clear All Statistics\n");
	fprintf(stderr, "\t Key Value Statistics Options\n");
	fprintf(stderr, "\t-k           Print KV Statistics on a key range\n");
	fprintf(stderr, "\t-h \'k\'|\'v\'[:L:U:B[:R:C]]\n");
	fprintf(stderr, "\t             Print KV histogram params for either keys or values\n");
	fprintf(stderr, "\t               L = lower bound; U = upper bound\n");
	fprintf(stderr, "\t               B = buckets\n");
	fprintf(stderr, "\t               R = rows to print; C = rows to clip\n");
	fprintf(stderr, "\t               defaults [k:0:maxkey:50:20:0][v:0:maxval:50:20:0]\n");
	fprintf(stderr, "\t-C           Print histogram as CSV\n");
	fprintf(stderr, "\t Key Value Statistics Range Options\n");
	fprintf(stderr, "\t-n count     Number of keys in range [unlimited]\n");
	fprintf(stderr, "\t-s KEY       Range start key, non inclusive\n");
	fprintf(stderr, "\t-S KEY       Range start key, inclusive\n");
	fprintf(stderr, "\t-e KEY       Range end key, non inclusive\n");
	fprintf(stderr, "\t-E KEY       Range end key, inclusive\n");
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
	char		c, *cp;
	int 		gets, puts, dels, noops, kvs, csv;
	struct histo	hkey, hval;
	int		tstats, clear;
	char 		*start = NULL, *end = NULL;
	int 		starti = 0, endi = 0;
        int 		count = KVR_COUNT_INF;
	kstats_t 	*kst;
	kstatus_t 	krc;
	int 		rc;
	struct histo 	*h;

	/* clear global flag vars */
	tstats = 0, clear = 0;
	gets = puts = dels = noops = kvs = csv = 0;

	hkey.h_lower   = hval.h_lower = 0;
	hkey.h_upper   = ka->ka_limits.kl_keylen;
	hval.h_upper   = ka->ka_limits.kl_vallen;
	hkey.h_buckets = hval.h_buckets = 50;
	hkey.h_rows    = hval.h_rows = 20;
	hkey.h_rclip   = hval.h_rclip = 0;
	hkey.h_histo   = hval.h_histo = NULL;
	
        while ((c = getopt(argc, argv, "Ccgh:pdNTks:S:e:E:n:?h")) != EOF) {
                switch (c) {
		case 'c':
		        clear = 1;
			break;
			
		case 'C':
		        csv = 1;
			break;

		case 'd':
			dels = 1;
			break;
			
		case 'g':
			gets = 1;
			break;
			
		case 'N':
			noops = 1;
			break;
			
		case 'p':
			puts = 1;
			break;
			
		case 'T':
			tstats = 1;
			break;
			
		case 'k':
			kvs = 1;
			break;

		case 'h':
			if ((optarg[0] == 'k') ||(optarg[0] == 'K')) {
				h = &hkey;
			} else if ((optarg[0] == 'v') ||(optarg[0] == 'V')) {
				h = &hval;
			} else {
				fprintf(stderr, "*** Invalid histo parms %s\n",
					optarg);
				CMD_USAGE(ka);
				return(-1);
			}

			/*
			 * if more in the arg process it,
			 * otherwise take the defaults
			 */
			if (optarg[1] != '\0') {
				rc = sscanf(&optarg[1],
					    "%[:]%i%[:]%i%[:]%i%[:]%i%[:]%i",
					    &c, &h->h_lower,
					    &c, &h->h_upper,
					    &c, &h->h_buckets,
					    &c, &h->h_rows,
					    &c, &h->h_rclip);

				if ((rc != 6) && (rc != 10))  {
					fprintf(stderr,
						"*** Invalid histo Parms %s\n",
						optarg);
					CMD_USAGE(ka);
					return(-1);
				}

				if (((int)h->h_lower   < 0) ||
				    ((int)h->h_upper   < 0) ||
				    ((int)h->h_buckets < 0) ||
				    ((int)h->h_rows    < 0) ||
				    ((int)h->h_rclip   < 0))  {
					fprintf(stderr,
                                                "*** Invalid histo parms < 0: %s\n",
                                                optarg);
                                        CMD_USAGE(ka);
                                        return(-1);
                                }

				if (h->h_lower >= h->h_upper) {
					fprintf(stderr,
                                                "*** Invalid histo lower bound %s\n",
                                                optarg);
                                        CMD_USAGE(ka);
                                        return(-1);
                                }

				if (h->h_rclip >= h->h_rows) {
					fprintf(stderr,
                                                "*** Invalid histo clip %s\n",
                                                optarg);
                                        CMD_USAGE(ka);
                                        return(-1);
                                }

			}

			h->h_histo = (uint32_t *)malloc(sizeof(uint32_t) * h->h_buckets);
			if (!h->h_histo) {
				fprintf(stderr, "*** Memory Failure\n");
				CMD_USAGE(ka);
				return(-1);
			}
			memset(h->h_histo, 0, (sizeof(uint32_t)*h->h_buckets));

			break;

		case 'n':
			if (optarg[0] == '-') {
				fprintf(stderr, "*** Negative count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}

			count = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0' || count==0) {
				fprintf(stderr, "*** Invalid count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;
			
		case 's':
			if (starti) {
				fprintf(stderr, "only one of -[sS]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			start = optarg;
			break;

		case 'S':
			if (start) {
				fprintf(stderr, "only one of -[sS]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			starti = 1;
			start = optarg;
			break;
			
		case 'e':
			if (end) {
				fprintf(stderr, "only one of -[eE]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			end = optarg;
			break;
		case 'E':
			if (endi) {
				fprintf(stderr, "only one of -[eE]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			end = optarg;
			endi = 1;
			break;
			
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	/* defining Start or End implies -k */
	if (end || start) kvs = 1;

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
			printf("Failed to Clear Statistics\n");
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

	if (kvs || hkey.h_histo || hval.h_histo)
		print_kvs(ka, kts, start, starti, end, endi, count,
			  &hkey, &hval, csv);

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

	printf("\tSend size, mean: %10.010g B (stddev=%g)\n",
	       kop->kop_ssize, kop->kop_sstdev);
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

static inline int
addto_histo(struct histo *h, size_t len)
{
	uint32_t bucket;
	float width;

	if (!h) return(-1);

	if (h->h_histo) {
		/* if not in bounds, bail. */
		if ((len < h->h_lower) || (len > h->h_upper))
			return(0);

		width = (h->h_upper - h->h_lower + 1) / (float)h->h_buckets;

		/* Adjust length by subtracting the lower bound */
		bucket = (int) ((len - h->h_lower) / width);
		assert(bucket < h->h_buckets);
		h->h_histo[bucket]++;
	}
	return(0);
}

static void
csv_histo(struct histo *h, char *msg)
{
	int i;

	if (!h) return;

	printf("\nHistogram\nData Set, %s\n", msg);

	printf("Lower Bound, %d\n", h->h_lower);
	printf("Upper Bound, %d\n", h->h_upper);
	printf("Buckets, %d\n", h->h_buckets);
	printf("Bucket width, %g\n",
	       (h->h_upper - h->h_lower + 1) / (float)h->h_buckets);
	printf("\nBucket, Count\n");

	for(i=0;i<h->h_buckets;i++)
		printf("%d, %d\n", i+1, h->h_histo[i]);
}

static void
ascii_histo(struct histo *h, char *msg)
{
	int r, b, clipped=0;
	float rheight, cheight;
	uint32_t maxcnt=0;

	if (!h) return;

	printf("\nHistogram: %s\n", msg);

	/* find the max countin the histo */
	for (b = 0; b < h->h_buckets; b++)
		if (h->h_histo[b] > maxcnt) maxcnt = h->h_histo[b];

	rheight = maxcnt / (float)h->h_rows;	/* Row height */

	/*
	 * Unicode codes: https://en.wikipedia.org/wiki/Block_Elements
	 */
	for (r = h->h_rows - 1; r >= 0; r--) {
		/*
		 * Clipping allows really spikey data to be mitigated.
		 * Let real size of histo to be tall but clip put N
		 * rows to let it fit on the screen. Otherwise details
		 * of lower count data is lost due to fitting a high spike
		 * into the alloted number of rows.
		 */
		if ((r < (h->h_rows - 1)) && (r > (h->h_rows - h->h_rclip))) {
			if (!clipped) {
				printf("%9s   [Clipping %d rows]\n",
				       "", h->h_rclip);
				clipped = 1;
			}
			continue;
		}

		cheight = rheight * r; /* Current Height */

		/* Y Axis */
		if ((r == (h->h_rows - 1)) || clipped) {
			if (clipped) {
				clipped = 0;
				printf("%9d\u2502", (uint32_t)cheight);
			} else {
				printf("%9d\u2502", maxcnt);
			}
		} else {
			printf("%9s\u2502", "");
		}

		/* Print the row */
		for (b = 0; b < h->h_buckets; b++) {
			if (h->h_histo[b] && h->h_histo[b] >= (int)cheight)
				printf("\u258C");
			else
				printf(" ");
		}
		printf("\n");

		if (r == 0) {
			/* X Axis */
			/* L char */
			printf("%9d\u2514", 0);
			/* Horizontal line */
			for (b = 0; b < h->h_buckets; b++) printf("\u2500");

			/* X-axis values: 2 rows; tens and ones */
			printf("\n%-10s", "");
			for (b = 1; b <= h->h_buckets; b++)
				if (b % 10 == 0)
					printf("%d", b/10);
				else
					printf(" ");

			printf("\n%-10s", "");
			for (b = 1; b <= h->h_buckets; b++)
				printf("%d", b%10);
		}
	}

	printf("\n\n%-10s","");
	printf("Lower Bound=%d, ", h->h_lower);
	printf("Upper Bound=%d, ", h->h_upper);
	printf("Buckets=%d, ", h->h_buckets);
	printf("Bucket width=%g\n",
	       (h->h_upper - h->h_lower + 1) / (float)h->h_buckets);

	return;
}

static int
print_kvs(struct kargs *ka, int kts,
	  char *start, int starti, char *end, int endi, int count,
	  struct histo *hkey, struct histo *hval, int csv)
{
	double		vlen_tot, vlen_mean, vlen_meansq, vlen_std;
	double		klen_tot, klen_mean, klen_meansq, klen_std;
	uint64_t	vlen_min, vlen_max, klen_min, klen_max;
	uint64_t	nkeys, nerrs, i, l;
	kstatus_t 	krc;
	krange_t	*kr;
	kiter_t		*kit;
	kv_t		*kv;
	struct kiovec	kv_val[1]   = {{0, 0}};
	struct kiovec	startkey[1] = {{0, 0}};
	struct kiovec	endkey[1] = {{0, 0}};
	struct kiovec	*k;

	/* If no start or end key given, its always inclusive */
	if (!start) starti = 1;
	if (!end)   endi   = 1;

	/*
	 * Setup the range
	 * If no start key provided, set kr_start to NULL
	 * If no end key provided, set kr_end to NULL
	 */
	if (!(kr = ki_create(kts, KRANGE_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	if (start) {
		kr->kr_start		= startkey;
		kr->kr_startcnt		= 1;

		/*
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if no escape sequences are
		 * present this amounts to a str copy.
		 */
		if (!asciidecode(start, strlen(start),
				 &kr->kr_start[0].kiov_base,
				 &kr->kr_start[0].kiov_len)) {
			fprintf(stderr, "*** Failed start key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
	}

	if (starti) {
		KR_FLAG_SET(kr, KRF_ISTART);
	}

	if (end) {
		kr->kr_end	= endkey;
		kr->kr_endcnt	= 1;

		/*
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if no escape sequences are
		 * present this amounts to a str copy.
		 */
		if (!asciidecode(end, strlen(end),
				 &kr->kr_end[0].kiov_base,
				 &kr->kr_end[0].kiov_len)) {
			fprintf(stderr, "*** Failed end key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
	}
	
	if (endi) {
		KR_FLAG_SET(kr, KRF_IEND);
	}

	kr->kr_count = ((count < 0)?KVR_COUNT_INF:count);

	/*
	 * If verbose dump the range we are getting.
	 * Keys can be large so just print first 5 chars
	 * of each key defining the range
	 * Use range notation for start, end:
	 * 	[ or ] = inclusive of the element,
	 * 	( or ) = exclusive of the element
	 */
	printf("Key Range %s", starti?"[":"(");
	if (!start) {
		printf("{FIRSTKEY}");
	} else {
		l = strlen(start);
		asciidump(start, (l>5?5:l));
	}

	printf(",");

	if (!end ) {
		printf("{LASTKEY}");
	} else {
		l = strlen(end);
		asciidump(end, (l>5?5:l));
	}

	printf("%s:",endi?"]":")");

	if (count > 0)
		printf("%u\n", count);
	else
		printf("unlimited\n");

	/* Create the kinetic range iterator */
	if (!(kit = ki_create(kts, KITER_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	/* Init kv */
	if (!(kv = ki_create(kts, KV_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	kv->kv_keycnt	= 1;
	kv->kv_val	= kv_val;
	kv->kv_valcnt	= 1;

	/* Iterate once to determine */
	nkeys=0;
	for (k = ki_start(kit, kr); k; k = ki_next(kit)) {
		nkeys++;
	}

	i = 0;
	nerrs = 0;
	for (k = ki_start(kit, kr); k; k = ki_next(kit)) {
		kv->kv_key = k;
		krc = ki_get(kts, kv);
		if (krc != K_OK) {
			nerrs++;
			continue;
		}

		i++;
		printf("analyzing ...%3lu%%\r", (i*100)/(nkeys-nerrs));

		addto_histo(hkey, k->kiov_len);
		addto_histo(hval, kv_val->kiov_len);

		if (i == 1) {
			vlen_min	= kv_val->kiov_len;
			vlen_max	= kv_val->kiov_len;
			vlen_tot	= (double)(kv_val->kiov_len);
			vlen_mean	= (double)(kv_val->kiov_len);
			vlen_meansq	= (double)0.0;

			klen_min	= k->kiov_len;
			klen_max	= k->kiov_len;
			klen_tot	= (double)(k->kiov_len);
			klen_mean	= (double)(k->kiov_len);
			klen_meansq	= (double)0.0;
		} else {
			double nmn, nsq;
			if (kv_val->kiov_len < vlen_min)
				vlen_min = kv_val->kiov_len;

			if (kv_val->kiov_len > vlen_max)
				vlen_max = kv_val->kiov_len;

			vlen_tot += (double)(kv_val->kiov_len);
			nmn = vlen_mean +   (kv_val->kiov_len - vlen_mean) / i;
			nsq = vlen_meansq + (kv_val->kiov_len - vlen_mean) *
				(kv_val->kiov_len - nmn);
			vlen_mean	= nmn;
			vlen_meansq	= nsq;

			if (k->kiov_len < klen_min)
				klen_min = k->kiov_len;

			if (k->kiov_len > klen_max)
				klen_max = k->kiov_len;

			klen_tot += (double)(k->kiov_len);
			nmn = klen_mean +   (k->kiov_len - klen_mean) / i;
			nsq = klen_meansq + (k->kiov_len - klen_mean) *
				(k->kiov_len - nmn);
			klen_mean	= nmn;
			klen_meansq	= nsq;
		}

		ki_clean(kv);
	}

	/* Calculate the Sample StdDev */
	vlen_std = sqrt(vlen_meansq/(nkeys - nerrs - 1));
	klen_std = sqrt(klen_meansq/(nkeys - nerrs - 1));

	printf("\n");
	printf("Total Keys            : %lu\n", nkeys);
	printf("Analyzed Keys         : %lu\n", nkeys - nerrs);
	printf("Failed Keys           : %lu\n", nerrs);

	printf("Key Length (min)      : %lu\n", klen_min);
	printf("Key Length (max)      : %lu\n", klen_max);
	printf("Key Length (mean)     : %-4.10g\n", klen_mean);
	printf("Key Length (stddev)   : %-4.10g\n", klen_std);

	printf("Value Length (min)    : %lu\n", vlen_min);
	printf("Value Length (max)    : %lu\n", vlen_max);
	printf("Value Length (mean)   : %-7.10g\n", vlen_mean);
	printf("Value Length (stddev) : %-7.10g\n", vlen_std);

	if (hkey->h_histo) {
		ascii_histo(hkey,  "Key Length");
		if (csv) csv_histo(hkey,  "Key Length");
	}
	if (hval->h_histo) {
		ascii_histo(hval,  "Value Length");
		if (csv) csv_histo(hval,  "Value Length");
	}

	if(start) free(kr->kr_start[0].kiov_base);

	ki_destroy(kv);
	ki_destroy(kr);
	ki_destroy(kit);
	return(0);
}
