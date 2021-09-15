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

#define CMD_USAGE(_ka) b_del_usage(_ka)

void
b_del_usage(struct bargs *ba)
{
	fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ba->ba_progname,ba->ba_cmdstr);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-n count     Delete <count> keys using common base key, KEY\n");
	fprintf(stderr, "\t-?           Help\n");

	fprintf(stderr, "\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nFor -n <count>, KEY is the base key and <count> keys are deleted\n");
	fprintf(stderr, "by appending an increasing sequence number to KEY. Ex. if KEY is\n");
	fprintf(stderr, "\"mykey\", then the first key deleted is \"mykey.000\". Used in\n");
	fprintf(stderr, "conjunction with put -l.\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
b_del(int argc, char *argv[], int ktd, struct bargs *ba)
{
	extern char	*optarg;
	extern int	optind, opterr, optopt;
	char		c, *cp;
	int		rc;
	uint64_t	count = 0;
	char		*rs;

	while ((c = getopt(argc, argv, "h?n:")) != (char)EOF) {
		switch (c) {
		case 'n':
			if (optarg[0] == '-') {
				fprintf(stderr, "*** Negative count %s\n",
				       optarg);
				CMD_USAGE(ba);
				return(-1);
			}
			
			count = strtoul(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "*** Invalid count %s\n",
				       optarg);
				CMD_USAGE(ba);
				return(-1);
			}
			
			if (count > ba->ba_limits.bkvl_maxn) {
				fprintf(stderr,
					"*** count too big (%lu > %ld)\n",
					count,  ba->ba_limits.bkvl_maxn);
				return(-1);
			}
			break;
			
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

        if (!count) {
		if ((rc = bkv_exists(ktd, ba->ba_key, ba->ba_keylen)) == 0) {
			printf("%s: Key not found.\n", ba->ba_cmdstr);
			return(1);
		}
	}
	
	if (count) {
		if (ba->ba_yes && !ba->ba_quiet) 
			printf("***DELETING %ld Key(s): ", count);
		else
			printf("***DELETE? %ld Key(s): ", count);
	} else {
		if (ba->ba_yes && !ba->ba_quiet) 
			printf("***DELETING Key: ");
		else
			printf("***DELETE? Key: ");
	}
	
	asciidump(ba->ba_key, ba->ba_keylen);
	printf("\n");
	
	/* ba_yes must be first to short circuit the conditional */
	if (ba->ba_yes || yorn("Please answer y or n [yN]: ", 0, 5)) {
		/* Delete the key */
		if (count)
			rc = bkv_deln(ktd, ba->ba_key, ba->ba_keylen, count);
		else 
			rc = bkv_del(ktd, ba->ba_key, ba->ba_keylen);
		
		if (rc < 0) {
			printf("%s: No key found.\n", ba->ba_cmdstr);
			return(rc);
		}
	}

	return(rc);
}

