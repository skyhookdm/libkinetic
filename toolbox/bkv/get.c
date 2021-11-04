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

#define CMD_USAGE(_ka) b_get_usage(_ka)

void
b_get_usage(struct bargs *ba)
{
	fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ba->ba_progname,ba->ba_cmdstr);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-n count     Get a single value assembled from count KEYs\n");
	fprintf(stderr, "\t-A           Dumps key/value as ascii w/escape seqs\n");
	fprintf(stderr, "\t-X           Dumps key/value as both hex and ascii\n");
	fprintf(stderr, "\t-?           Help\n");

	fprintf(stderr, "\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, "Only \\xHH escape sequences are converted, ex \\xF8.");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nFor -n <count>, KEY is the base key and <count> keys are retrieved\n");
	fprintf(stderr, "by appending an increasing sequence number to KEY. Ex. if KEY is\n");
	fprintf(stderr, "\"mykey\", then the first key retrieved is \"mykey.000\". Used in\n");
	fprintf(stderr, "conjunction with put -l.\n");

	fprintf(stderr, "\nBy default keys and values are printed as raw strings,\n");
	fprintf(stderr, "including special/nonprintable chars\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
b_get(int argc, char *argv[], int ktd, struct bargs *ba)
{
	extern char	*optarg;
	extern int	optind, opterr, optopt;
	char		c, *cp;
	int		rc, hdump = 0, adump = 0;
	uint64_t	count = 0;
	char		*rkey, *rs;

	while ((c = getopt(argc, argv, "n:AXh?")) != EOF) {
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
					"*** count too big (%llu > %ld)\n",
					count,  ba->ba_limits.bkvl_maxn);
				return(-1);
			}
			break;
			
		case 'A':
			adump = 1;
			if (hdump) {
				fprintf(stderr,
					"*** -A and -X are exclusive\n");
				CMD_USAGE(ba);
				return (-1);
			}
			break;

		case 'X':
			hdump = 1;
			if (adump) {
				fprintf(stderr,
					"*** -X and -A are exclusive\n");
				CMD_USAGE(ba);
				return (-1);
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

	/* Get the keys or key */
	if (count) {
		rc = bkv_getn(ktd, ba->ba_key, ba->ba_keylen, (uint32_t)count,
			     (void **)&ba->ba_val, &ba->ba_vallen);
	} else {
		rc = bkv_get(ktd, ba->ba_key, ba->ba_keylen,
			     (void **)&ba->ba_val, &ba->ba_vallen);
	}

	if (rc < 0) {
		printf("%s: No key found.\n", ba->ba_cmdstr);
		return(rc);
	}

	/* Print key name */
	if (!ba->ba_quiet) {
		printf("Key(%lu):",  ba->ba_keylen);
		if (adump) {
			printf("\n");
			asciidump(ba->ba_key, ba->ba_keylen);
			printf("\n");
		} else if (hdump) {
			printf("\n");
			hexdump(ba->ba_key, ba->ba_keylen);
		} else {
			/* add null byte to print as a string */
			rkey = strndup(ba->ba_key, ba->ba_keylen);
			printf("\n%s\n", rkey);
			free(rkey);
		}
		printf("Value(%lu):\n",  ba->ba_vallen);
	}

	/* Print the value */

	if (adump) {
		asciidump(ba->ba_val, ba->ba_vallen);
		if (!ba->ba_quiet) {
			printf("\n");
		}
	} else if (hdump) {
		hexdump(ba->ba_val, ba->ba_vallen);
	} else {
		/* raw */
		write(fileno(stdout),ba->ba_val, ba->ba_vallen);
		if (!ba->ba_quiet) {
			printf("\n");
		}
	}

	return(rc);
}

