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

#define CMD_USAGE(_ka) kctl_pin_usage(_ka)

void
kctl_pin_usage(struct kargs *ka)
{
	fprintf(
		stderr,
		"Usage: %s [..] %s {-e|-l} -n <str> [CMD OPTIONS]\n",
		ka->ka_progname,
		ka->ka_cmdstr
	);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-e           Set the Erase PIN\n");
	fprintf(stderr, "\t-l           Set the Lock PIN\n");
	fprintf(stderr, "\t-p <string>  Current PIN for device operation\n");
	fprintf(stderr, "\t-n <string>  Current PIN for device operation\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

#define CMD_USAGE(_ka) kctl_pin_usage(_ka)


/* Set pins for lock and erase on a kinetic server */
int
kctl_pin(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *pin = NULL, *npin = NULL;
	size_t		pinlen = 0, npinlen = 0; 
	kstatus_t	krc;
	kpin_type_t	ptype = KPIN_INVALID;
	
        while ((c = getopt(argc, argv, "h?elp:n:")) != EOF) {
                switch (c) {
		case 'e':
			if (ptype != KPIN_INVALID){
				fprintf(stderr, "*** Pin already set\n");
				return(-1);
			}
			ptype = KPIN_ERASE;
			break;

		case 'l':
			if (ptype != KPIN_INVALID){
				fprintf(stderr, "*** Pin already set\n");
				return(-1);
			}
			ptype = KPIN_LOCK;
			break;

		case 'p':
			pin = optarg;
			pinlen = strlen(pin);
			break;

		case 'n':
			npin = optarg;
			npinlen = strlen(npin);
			break;

		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	if (!ka->ka_usetls) {
		fprintf(stderr, "*** Setting a PIN requires a SSL/TLS\n");
		return(-1);
	}

	// Check for correct operation
	if (argc - optind != 0) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	if ((ptype == KPIN_INVALID) || !npin ) {
		fprintf(stderr, "*** Missing args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	krc = ki_setpin(ktd, pin, pinlen, npin, npinlen, ptype);
	if (krc != K_OK) {
		printf("kinetic pin op failed: status=%d %s\n",
		       krc, ki_error(krc));
		return(-1);
	}
	return(0);
}
