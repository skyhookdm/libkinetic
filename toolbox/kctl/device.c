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

#define CMD_USAGE(_ka) kctl_device_usage(_ka)

void
kctl_device_usage(struct kargs *ka)
{
	fprintf(
		stderr,
		"Usage: %s [..] %s [CMD OPTIONS] OPERATION\n",
		ka->ka_progname,
		ka->ka_cmdstr
	);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-p <string>  Secret PIN for device operation\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nWhere, OPERATION is one of:\n");
	fprintf(stderr, "\tlock         Lock the entire device\n");
	fprintf(stderr, "\tunlock       Unlock the entire device\n");
	fprintf(stderr, "\terase        Erase all key values\n");
	fprintf(stderr, "\tise          Instant Secure Erase - not available on all devices\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

#define CMD_USAGE(_ka) kctl_device_usage(_ka)


/* Perform a device operation on the kinetic server */
int
kctl_device(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *pin = NULL;
	size_t		pinlen = 0; 
	int		tlsck = 1;	/* XXX hidden flag for issue #95 */
	kstatus_t	krc;
	kdevop_t	op;
		
        while ((c = getopt(argc, argv, "h?p:t")) != EOF) {
                switch (c) {
		case 'p':
			pin = optarg;
			pinlen = strlen(pin);
			break;
			
		case 't':
			/*
			 * turn off tlsck for issue #95 recreation.
			 * This flag should be removed when #95 is resolved
			 */
			tlsck = 0;
			break;

		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	if (tlsck && !ka->ka_usetls) {
		fprintf(stderr, "*** Device operations must use SSL/TLS\n");
		return(-1);
	}

	// Check for correct operation
	if (argc - optind == 1) {
		optarg = argv[optind];
		if (!(strcmp(optarg, "lock"))) {
			op = KDO_LOCK;
		} else if (!(strcmp(optarg, "unlock"))) {
			op = KDO_UNLOCK;
		} else if (!(strcmp(optarg, "erase"))) {
			op = KDO_ERASE;
		} else  if (!(strcmp(optarg, "ise"))) {
			op = KDO_SECURE_ERASE;
		} else {
			fprintf(stderr, "*** Bad operation arg: %s\n", optarg);
			CMD_USAGE(ka);
			return(-1);
		}
	} else if (argc - optind > 1) {      
		fprintf(stderr, "*** Too many operation args\n");
		CMD_USAGE(ka);
		return(-1);
	} else {
		fprintf(stderr, "*** Missing operation arg\n");
		CMD_USAGE(ka);
		return(-1);
	}

	krc = ki_device(ktd, pin, pinlen, op);
	if (krc != K_OK) {
		printf("kinetic device op failed: status=%d %s\n",
		       krc, ki_error(krc));
		return(-1);
	}
	return(0);
}
