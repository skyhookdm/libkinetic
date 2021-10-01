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

#define CMD_USAGE(_ka) kctl_flush_usage(_ka)

void 
kctl_flush_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	char msg[] = "\n\
Where, CMD OPTIONS are [default]:\n\
	-?           Help\n\
\n\
To see available COMMON OPTIONS: ./kctl -?\n";

	fprintf(stderr, "%s", msg);
}

/**
 *  Issue a ki_flush()
 */
int
kctl_flush(int argc, char *argv[], int ktd, struct kargs *ka)
{
	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c;
	kstatus_t 	krc;
	
        while ((c = getopt(argc, argv, "?h")) != (char)EOF) {
                switch (c) {
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


	krc = ki_flush(ktd);
	if (krc != K_OK) {
		printf("Flush failed\n");
		return(-1);
	}

	return(0);
}

