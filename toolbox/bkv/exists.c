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

#define CMD_USAGE(_ka) b_exists_usage(_ka)

void
b_exists_usage(struct bargs *ba)
{
	fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ba->ba_progname,ba->ba_cmdstr);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-?           Help\n");

	fprintf(stderr, "\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
b_exists(int argc, char *argv[], int ktd, struct bargs *ba)
{
	extern char	*optarg;
	extern int	optind, opterr, optopt;
	char		c;
	int		rc;
	char		*rs;

	while ((c = getopt(argc, argv, "h?")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
		default:
			CMD_USAGE(ba);
			return (-1);
		}
	}

	// Check for the cmd key parm
	if (argc - optind == 1) {
		/*
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if none present this
		 * amounts to a copy.
		 */
		rs = asciidecode( argv[optind], strlen(argv[optind]),
				  (void **) &ba->ba_key, &ba->ba_keylen);

		if (!rs) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ba);
			return (-1);
		}
#if 0
		printf("%s\n", argv[optind]);
		printf("%lu\n", ba->ba_keylen);
		hexdump(ba->ba_key, ba->ba_keylen);
#endif
	} else {
		fprintf(stderr, "*** Too few or too many args\n");
		CMD_USAGE(ba);
		return (-1);
	}

	/* 
	 * Does the key exist? 
	 * rc will become the exit code, reverse it
	 */
	rc = bkv_exists(ktd, ba->ba_key, ba->ba_keylen);
	if (rc == 1) {
		rc = 0;
		if (!ba->ba_quiet)
			printf("Key exists\n");
	} else if (rc == 0) {
		rc = 1;
		if (!ba->ba_quiet)
			printf("Key doesn't exist\n");
	} else {
		fprintf(stderr, "*** Failed key operation\n");
	}

	return(rc);
}

