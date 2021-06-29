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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define CMD_USAGE(_ka) kctl_range_usage(_ka)

void
kctl_range_usage(struct kargs *ka)
{
	fprintf(stderr
		,"Usage: %s [..] %s [CMD OPTIONS]\n"
		,ka->ka_progname, ka->ka_cmdstr
	);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-n count     Number of keys in range [unlimited]\n");
	fprintf(stderr, "\t-s KEY       Range start key, non inclusive\n");
	fprintf(stderr, "\t-S KEY       Range start key, inclusive\n");
	fprintf(stderr, "\t-e KEY       Range end key, non inclusive\n");
	fprintf(stderr, "\t-E KEY       Range end key, inclusive\n");
	fprintf(stderr, "\t-r           Reverse the order\n");
	fprintf(stderr, "\t-A           Show keys as encoded ascii strings\n");
	fprintf(stderr, "\t-X           Show keys as hex and ascii\n");
	fprintf(stderr, "\t-?           Help\n");

	fprintf(stderr,
		"\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr,
		"hexidecimal escape sequences to encode binary characters.\n");

	// R"foo(....)foo" is a non escape char evaluated string literal 
	fprintf(stderr,
		R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr,
		"\nIf a conversion fails the command terminates.\n");

	fprintf(stderr,
		"\nIf no start (-sS] or stop (-sS) key is given, it is\n");
	fprintf(stderr,
		"equivalent to: -S <First Key> and -E <Last Key>\n");
	fprintf(stderr,
		"\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
kctl_range(int argc, char *argv[], int ktd, struct kargs *ka)
{
	extern char	*optarg;
	extern int	optind, opterr, optopt;
	char		c, *cp, *rkey;
	char		*start = NULL, *end = NULL;
	int		i;
	int		starti  = 0, endi = 0;
	int		count   = KVR_COUNT_INF;
	int		reverse = 0;
	int		adump   = 0, hdump = 0;
	kstatus_t 	krc;
	krange_t	*kr;
	kiter_t		*kit;
	struct kiovec	startkey[1] = {{0, 0}};
	struct kiovec	endkey[1]   = {{0, 0}};
	struct kiovec	*k;
	
	while ((c = getopt(argc, argv, "rs:S:e:E:n:AXh?")) != EOF) {

		switch (c) {
		case 'r':
			reverse = 1;
			break;

		case 'n':
			if (optarg[0] == '-') {
				fprintf(stderr, "*** Negative count %s\n", optarg);
				CMD_USAGE(ka);
				return(-1);
			}

			count = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0' || count==0) {
				fprintf(stderr, "*** Invalid count %s\n", optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;

		case 's':
			if (starti) {
				fprintf(stderr, "only one of -[sS]\n");
				CMD_USAGE(ka);
				return (-1);
			}

			start = optarg;
			break;

		case 'S':
			if (start) {
				fprintf(stderr, "only one of -[sS]\n");
				CMD_USAGE(ka);
				return(-1);
			}

			starti = 1;
			start  = optarg;
			break;

		case 'e':
			if (end) {
				fprintf(stderr, "only one of -[eE]\n");
				CMD_USAGE(ka);
				return(-1);
			}

			end = optarg;
			break;

		case 'E':
			if (endi) {
				fprintf(stderr, "only one of -[eE]\n");
				CMD_USAGE(ka);
				return(-1);
			}

			end  = optarg;
			endi = 1;
			break;

		case 'A':
			adump = 1;
			if (hdump) {
				fprintf(stderr, "**** -X and -A are exclusive\n");
				CMD_USAGE(ka);
				return(-1);
			}

			break;
		case 'X':
			hdump = 1;
			if (adump) {
				fprintf(stderr, "**** -X and -A are exclusive\n");
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

	/* Check for erroneous params */
	if (argc - optind > 0) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	/* If no start or end key given, its always inclusive */
	if (!start) starti = 1;
	if (!end)   endi   = 1;

	/*
	 * Setup the range
	 * If no start key provided, set kr_start to NULL
	 * If no end key provided, set kr_end to NULL 
	 */
	if (!(kr = ki_create(ktd, KRANGE_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	if (start) {
		kr->kr_start		= startkey;
		kr->kr_startcnt		= 1;

		/*
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if no escape sequences are
		 * present this amounts to a str copy.
		 */
		if (!asciidecode(start, strlen(start),
				 &kr->kr_start[0].kiov_base,
				 &kr->kr_start[0].kiov_len)) {
			fprintf(stderr, "*** Failed start key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
	}

	if (starti) {
		KR_FLAG_SET(kr, KRF_ISTART);
	}
	
	if (end) {
		kr->kr_end	= endkey;
		kr->kr_endcnt	= 1;

		/*
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if no escape sequences are
		 * present this amounts to a str copy.
		 */
		if (!asciidecode(end, strlen(end),
				 &kr->kr_end[0].kiov_base,
				 &kr->kr_end[0].kiov_len)) {
			fprintf(stderr, "*** Failed end key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
	}
	
	if (endi) {
		KR_FLAG_SET(kr, KRF_IEND);
	}

	if (reverse) {
		KR_FLAG_SET(kr, KRF_REVERSE);
	}

	kr->kr_count = (count < 0) ? KVR_COUNT_INF : count;

	/*
	 * If verbose dump the range we are getting.
	 * Keys can be large so just print first 5 chars 
	 * of each key defining the range
	 * Use range notation for start, end:
	 *  [ or ] = inclusive of the element,
	 *  ( or ) = exclusive of the element
	 */
	if (ka->ka_verbose)  {
		int l;
		printf("Key Range %s", starti?"[":"(");

		if (!start) {
			printf("{FIRSTKEY}");
		}
		else {
			l = strlen(start);
			if (adump) {
				asciidump(start, (l>5?5:l));
			}
			else {
				if (l > 5) { printf("%.5s", start); }
				else       { printf("%s"  , start); }
			}
		}

		printf(",");

		if (!end ) {
			printf("{LASTKEY}");
		}
		else {
			l = strlen(end);
			if (adump)
				asciidump(end, (l>5?5:l));
			else {
				if (l > 5) { printf("%.5s", end); }
				else       { printf("%s"  , end); }
			}
		}

		printf("%s:",endi?"]":")");

		if (count > 0) { printf("%u\n", count); }
		else           { printf("unlimited\n"); }
	}

	/*
	 * Iterate over all the keys and print them out
	 *
	 * If 0 < count <= MAX keys per range call (count = -1 is unlimited)
	 * then use a single ki_range call, no need to iterate.
	 * Of course this is unnecessary but allows the caller to test
	 * ki_range call directly without going through the key iterator code.
	 */
	if ((count > 0) && (count <= ka->ka_limits.kl_rangekeycnt)) {
		if (ka->ka_verbose) { printf("Single Range Call...\n"); }

		krc = ki_getrange(ktd, kr);
		if (krc != K_OK) {
			fprintf(stderr
				,"%s: Unable to get key range: %s\n"
				,ka->ka_cmdstr, ki_error(krc)
			);

			return (-1);
		}

		if (!kr->kr_keyscnt) {
			printf ("No Keys Found.\n");
			return (0);
		}

		for (int i = 0; i < kr->kr_keyscnt; i++) {
			if (ka->ka_verbose) {
				printf("%u: ", i);
			}

			if (hdump) {
				hexdump(
					 (char *) kr->kr_keys[i].kiov_base
					,         kr->kr_keys[i].kiov_len
				);
			}

			else if (adump) {
				asciidump(
					 (char *) kr->kr_keys[i].kiov_base
					,         kr->kr_keys[i].kiov_len
				);
				printf("\n");
			}

			else {
				/* add null byte to print ads a string */
				rkey = strndup(
					 (char *) kr->kr_keys[i].kiov_base
					,         kr->kr_keys[i].kiov_len
				);

				printf("%s\n", rkey);
				free(rkey);
			}
		}

		/* Success so return */
		return (0);
	}

	/*
	 * Number of keys requested are larger than a single ki_range call
	 * can return, so use IterateKeyRangethe key iterator.
	 */
	if (ka->ka_verbose) { printf("Iterating...\n"); }

	// Create the kinetic range iterator
	if (!(kit = ki_create(ktd, KITER_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	// Iterate
	i = 0;

	for (k = ki_start(kit, kr); k; k = ki_next(kit)) {
		if (ka->ka_verbose) {
			printf("%u: ", i++);
		}

		// Dump the key
		if (hdump) {
			hexdump(k->kiov_base, k->kiov_len);
		}

		else if (adump) {
			asciidump(k->kiov_base, k->kiov_len);
			printf("\n");
		}

		else {
			// add null byte to print as a string
			rkey = strndup((char *) k->kiov_base, k->kiov_len);
			printf("%s\n", rkey);
			free(rkey);
		}
	}

	// Destroy the iterator we created
	ki_destroy(kit);

	// Clear stack-based data, then destroy original key range
	kr->kr_start    = NULL;
	kr->kr_startcnt = 0;
	kr->kr_end      = NULL;
	kr->kr_endcnt   = 0;
	ki_destroy(kr);

	return(0);
}

