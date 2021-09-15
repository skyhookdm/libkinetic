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
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/basickv.h>
#include "bkv.h"

#define CMD_USAGE(_ka) b_limits_usage(_ka)

void
b_limits_usage(struct bargs *ba)
{
	fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ba->ba_progname,ba->ba_cmdstr);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-?           Help\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
b_limits(int argc, char *argv[], int ktd, struct bargs *ba)
{
	extern char	*optarg;
	extern int	optind, opterr, optopt;
	char		c;
	int		rc;
	bkv_limits_t 	bl;
	
	while ((c = getopt(argc, argv, "h?n:")) != (char)EOF) {
		switch (c) {
		case 'h':
		case '?':
		default:
			CMD_USAGE(ba);
			return (-1);
		}
	}

	// Check for the cmd key parm
	if (argc - optind != 0) {
		fprintf(stderr, "*** Too few or too many args\n");
		CMD_USAGE(ba);
		return (-1);
	}

	rc = bkv_limits(ktd, &bl);
	if (rc < 0) {
			printf("%s: Get limits failed.\n", ba->ba_cmdstr);
			return(rc);
	}

	printf("Limits:\n");
	printf("\tMax Key Length: %lu\n", bl.bkvl_klen);
	printf("\tMax Val Length: %lu\n", bl.bkvl_vlen);
	printf("\tMax N:          %lu\n", bl.bkvl_maxn);

	return(0);
}

