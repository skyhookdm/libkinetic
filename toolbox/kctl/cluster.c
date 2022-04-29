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
#include <sys/stat.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define CMD_USAGE(_ka) kctl_cluster_usage(_ka)

void
kctl_cluster_usage(struct kargs *ka)
{
	fprintf(stderr,	"Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);

	char msg[] = "\n\
Where, CMD OPTIONS are [default]:\n\
	-s <version>  Set the cluster version, must be a number\n\
	-?           Help\n\
\n\
To see available COMMON OPTIONS: ./kctl -?\n";

	fprintf(stderr, "%s", msg);
}

#define CMD_USAGE(_ka) kctl_cluster_usage(_ka)


/* Set and get the cluster version on a kinetic server */
int
kctl_cluster(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *cp;
	kstatus_t	krc;
	int		set=0;
	int64_t		vers, curvers;

        while ((c = getopt(argc, argv, "s:h?")) != (char)EOF) {
                switch (c) {
		case's':
			vers = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "**** Invalid version: %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			set = 1;
			break;
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	/* Check for correct arguments */
	if (argc - optind != 0) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	krc = ki_getclustervers(ktd, &curvers);
	if (krc != K_OK) {
		printf("Get cluster version failed: status=%d %s\n",
		       krc, ki_error(krc));
		return(-1);
	}

        if (!set) {
		printf("Cluster Version: %ld\n", curvers);
		return(0);
	}

	/* Setting a new cluster version */
	if (curvers == vers) {
		if (!ka->ka_quiet) {
			printf("Requested cluster version equals current ");
			printf("version. No action taken\n");
		}
		return(0);
	}

	if (ka->ka_yes) {
		if (!ka->ka_quiet)
			printf("*** Setting cluster version from %ld to %ld\n",
			       curvers, vers);
	} else {
		printf("*** Set cluster version  from %ld to %ld?\n",
		       curvers, vers);
	}

	/* ka_yes must be first to short circuit the conditional */
	if (ka->ka_yes || yorn("Please answer y or n [yN]: ", 0, 5)) {

		krc = ki_setclustervers(ktd, vers);
		if (krc != K_OK) {
			printf("Set cluster version failed: status=%d %s\n",
			       krc, ki_error(krc));
			return(-1);
		}
	}

	return(0);
}
