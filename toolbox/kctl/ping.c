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
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define CMD_USAGE(_ka) kctl_ping_usage(_ka)

void
kctl_ping_usage(struct kargs *ka)
{
	fprintf(
		stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ka->ka_progname,
		ka->ka_cmdstr
	);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-n count     Number of key copies to make [1]\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

#define CMD_USAGE(_ka) kctl_ping_usage(_ka)


// Ping the kinetic server
int
kctl_ping(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *cp;
	int 		i, s, count = 1;
	uint64_t 	t, tt, min, max;
	double 		m, sq;
	kstatus_t	krc;
	struct timespec start, stop;
	
        while ((c = getopt(argc, argv, "h?n:")) != EOF) {
                switch (c) {
		case 'n':
			if (optarg[0] == '-') {
				fprintf(stderr, "*** Negative count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}

			count = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "**** Invalid count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			if (count > 10000000) {
				fprintf(stderr, "**** Count too large %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	// Check for erroneous params
	if (argc - optind > 0) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	// A noop hits the server and return status
	tt=0; s=0;
	printf("PING Kinetic service on %s:%s\n", ka->ka_host, ka->ka_port);
	for(i=0; i<count; i++) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		krc = ki_noop(ktd);
		clock_gettime(CLOCK_MONOTONIC, &stop);
		ts_sub(&stop, &start, t);
		
		if (krc == K_OK) {
			s++;
		} else {
			printf("kinetic noop %s: seq=%d failed: status=%d %s\n",
			       ka->ka_host, i+1, krc, ki_error(krc));
			continue;
		}

		if (i==0) {
			tt = min = max = t;
			m = (double) t;
			sq = 0.0;
		} else {
			double nm, nsq;
			tt += t;
			if (t < min) min = t;
			if (t > max) max = t;
			nm  =  m + (t - m) / (i + 1);
			nsq = sq + (t - m) * (t - nm);
			m   = nm;
			sq  = nsq;
		}

		printf("kinetic noop %s: seq=%d time=%lu \xC2\xB5S\n",
		       ka->ka_host, i+1, t);
	}		
	printf("%d noops transmitted, %d received, %2.3f%% missed, ",
	       i, s, (i-s)/(float)i*100);
	printf("time=%lu \xC2\xB5S\n", tt);
	printf("rtt min/avg/max/mdev = %lu/%1.3g/%lu/%1.3g \xC2\xB5S\n",
	       min, m, max, sqrt(sq/(i-1)));

	if (tt)
		/* if one makes it through, then return ok status */
		return(0); 

	return(-1);
}
