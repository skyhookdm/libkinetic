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

#include <kinetic/kinetic.h>
#include "kctl.h"

/* Cheating with globals */
static char	create, submit, abort;

void kctl_dump(kgetlog_t *glog);

#define CMD_USAGE(_ka) kctl_batch_usage(_ka)

void 
kctl_batch_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-C           Create a batch\n");
	fprintf(stderr, "\t-S           Submit a batch\n");
	fprintf(stderr, "\t-A           Abort a batch\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

/**
 *  Issue either the batchstart or batchend command
 */
int
kctl_batch(int argc, char *argv[], int kts, struct kargs *ka)
{
	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c;
	kstatus_t 	krc;
	
	/* clear global flag vars */
	create = submit = abort = 0;

        while ((c = getopt(argc, argv, "SCA?h")) != EOF) {
                switch (c) {
		case 'C':
			create = 1;
			if (ka->ka_batch) {
				fprintf(stderr,
					"*** Active batch already exists\n");
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'S':
			submit = 1;
			if (!ka->ka_batch) {
				fprintf(stderr,
					"*** No active batch to Submit\n");
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'A':
			abort = 1;
			if (!ka->ka_batch) {
				fprintf(stderr,
					"*** No active batch to Abort\n");
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
	
	if (!create && !submit && !abort) {
                        fprintf(stderr,
				"*** Must Create, Submit, or Abort\n");
			CMD_USAGE(ka);
			return(-1);
	}
	
	if ((create + submit + abort) != 1) {
                        fprintf(stderr,
				"*** Only one Create, Submit or Abort\n");
			CMD_USAGE(ka);
			return(-1);
	}
 	
	/* Shouldn't be any other args */
	if (argc - optind) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	if (create) {
		ka->ka_batch = ki_create(kts, KBATCH_T);
		if (!ka->ka_batch) {
			printf("Batch Start failed\n");
			return(-1);
		}
	} else if (submit) {
		krc = ki_submitbatch(kts, ka->ka_batch);
		
		if (krc == K_OK) {
			ka->ka_batch = NULL;
		} else {
			printf("Batch commit failed: %s\n", ki_error(krc));
			return(-1);
		}
	} else  {
		krc = ki_abortbatch(kts, ka->ka_batch);
		ka->ka_batch = NULL;

		if (krc == K_OK) {
			ka->ka_batch = NULL;
		} else {
			printf("Batch abort failed: %s\n", ki_error(krc));
			return(-1);
		}
	}

	return(0);
}

