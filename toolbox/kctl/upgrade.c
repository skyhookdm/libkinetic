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

#define CMD_USAGE(_ka) kctl_upgrade_usage(_ka)

void
kctl_upgrade_usage(struct kargs *ka)
{
	fprintf(stderr,	"Usage: %s [..] %s [CMD OPTIONS] <firmware file>\n",
		ka->ka_progname, ka->ka_cmdstr);

	char msg[] = "\n\
Where, CMD OPTIONS are [default]:\n\
	-?           Help\n\
\n\
To see available COMMON OPTIONS: ./kctl -?\n";

	fprintf(stderr, "%s", msg);
}

#define CMD_USAGE(_ka) kctl_upgrade_usage(_ka)


/* Upgrade the firmware on a kinetic server */
int
kctl_upgrade(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *filename=NULL;
	struct stat	st;
	size_t		fwlen = 0;
	void		*fw;
	kstatus_t	krc;
	int		fd;

        while ((c = getopt(argc, argv, "h?")) != (char)EOF) {
                switch (c) {
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

#if 0
	if (!ka->ka_usetls) {
		fprintf(stderr, "*** Upgrading requires SSL/TLS\n");
		return(-1);
	}
#endif

	/* Check for correct arguments */
	if (argc - optind != 1) {
		fprintf(stderr, "*** Too few/many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	filename = argv[optind];

	if (stat(filename, &st) < 0) {
		perror("stat");
		fprintf(stderr, "*** Error accessing file %s\n",
			filename);
		CMD_USAGE(ka);
		return(-1);
	}

	if (st.st_size > (size_t)ka->ka_limits.kl_vallen) {
		fprintf(stderr, "*** file too long (%lu > %d)\n",
			st.st_size, ka->ka_limits.kl_vallen);
		return(-1);
	}

	fwlen	= st.st_size;
	fw	= (char *) malloc(fwlen);
	if (!fw) {
		fprintf(stderr, "*** Unable to alloc %lu bytes\n", fwlen);
		CMD_USAGE(ka);
		return(-1);
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "*** Unable to open file %s\n",	filename);
		CMD_USAGE(ka);
		return(-1);
	}

	if (read(fd, fw, fwlen) != fwlen){
		fprintf(stderr, "*** Unable to read file %s\n",	filename);
		CMD_USAGE(ka);
		return(-1);
	}

	close(fd);

	if (ka->ka_yes) {
		if (!ka->ka_quiet)
			printf("***Upgrading with Firmware in %s!!\n",
			       filename);
	} else {
		printf("***Upgrade with Firmware in %s?\n", filename);
	}

	/* ka_yes must be first to short circuit the conditional */
	if (ka->ka_yes || yorn("Please answer y or n [yN]: ", 0, 5)) {

		krc = ki_firmware(ktd, fw, fwlen);
		if (krc != K_OK) {
			printf("kinetic upgrade failed: status=%d %s\n",
			       krc, ki_error(krc));
			return(-1);
		}
		if (!ka->ka_quiet)
			printf("Upgrade performed.\n");
	} else {
		if (!ka->ka_quiet)
			printf("No upgrade performed.\n");
	}

	return(0);
}
