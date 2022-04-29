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
#include <time.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define HAVE_RANGE ((count>-1)||start||starti||end||endi||all)
#define VERLEN 11

#define CMD_USAGE(_ka) kctl_del_usage(_ka)

void
kctl_del_usage(struct kargs *ka)
{
	fprintf(stderr,
		"Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ka->ka_progname, ka->ka_cmdstr
	);

	char msg[] = "\n\
Where, CMD OPTIONS are [default]:\n\
	-b           Add to current batch [no]\n\
	-c           Compare and delete [no]\n\
	-p [wt|wb|f] Persist Mode: writethrough, writeback, \n\
	             flush [writeback]\n\
	-F           Issue a flush at completion\n\
	-a           Delete all keys\n\
	-n count     Number of keys in range [unlimited]\n\
	-s KEY       Range start key, non inclusive\n\
	-S KEY       Range start key, inclusive\n\
	-e KEY       Range end key, non inclusive\n\
	-E KEY       Range end key, inclusive\n\
	-?           Help\n\
\n\
Where, KEY is a quoted string that can contain arbitrary\n\
hexidecimal escape sequences to encode binary characters.\n\
Only \\xHH escape sequences are converted, ex \\xF8.\n\
If a conversion fails the command terminates.\n\
\n\
To see available COMMON OPTIONS: ./kctl -?\n";

	fprintf(stderr, "%s", msg);
}

/*
 * Delete a Key Value pair, a range of Key Value pairs, or all Key Value pairs
 *
 * By default the version number is stored as 8 digit hexidecimal number
 * starting at 0 for new keys.
 *
 * By default versions are ignored, compare and delete can be enabled with -c
 * This enforces the correct version is passed to the delete or else it fails
 *
 * All persistence modes are supported with -p [wt,wb,f] defaulting to
 * WRITE_BACK
 */
int
kctl_del(int argc, char *argv[], int ktd, struct kargs *ka)
{
	extern char     *optarg;
	extern int       optind, opterr, optopt;

	char             c, *cp;
	kcachepolicy_t   cpolicy = KC_WB;
	char            *start = NULL, *end = NULL;

	int              starti = 0, endi = 0;
	int              all = 0;
	int              dels = 0, count = KVR_COUNT_INF;
	int              cmpdel = 0, bat=0, flush=0;

	kv_t            *kv;
	krange_t        *kr;
	kiter_t         *kit;

	struct kiovec    kv_key[1]   = {{0, 0}};
	struct kiovec    kv_val[1]   = {{0, 0}};
	struct kiovec    startkey[1] = {{0, 0}};
	struct kiovec    endkey[1]   = {{0, 0}};
	struct kiovec   *k;
	kstatus_t        krc;
	struct timespec  tstart, tstop;

	while ((c = getopt(argc, argv, "abcFp:s:S:e:E:n:h?")) != (char)EOF) {
		switch (c) {
			case 'b':
				bat = 1;
				if (!ka->ka_batch) {
					fprintf(stderr, "**** No active batch\n");
					CMD_USAGE(ka);
					return(-1);
				}
				break;

			case 'c':
				cmpdel = 1;
				break;

			case 'F':
				flush = 1;
				break;

			case 'p':
				if (strlen(optarg) > 2) {
					fprintf(stderr, "**** Bad -p flag option %s\n", optarg);
					kctl_del_usage(ka);
				}

				if (strncmp(optarg, "wt", 2) == 0)
					cpolicy = KC_WT;
				else if (strncmp(optarg, "wb", 2) == 0)
					cpolicy = KC_WB;
				else if (strncmp(optarg, "f", 1) == 0)
					cpolicy = KC_FLUSH;
				else {
					fprintf(stderr, "**** Bad -p flag option: %s\n", optarg);
					CMD_USAGE(ka);
					return(-1);
				}

				break;

			case 'n':
				if (all) {
					fprintf(stderr, "**** can't have a count with -a\n");
					CMD_USAGE(ka);
					return (-1);
				}

				if (optarg[0] == '-') {
					fprintf(stderr, "*** Negative count %s\n", optarg);
					CMD_USAGE(ka);
					return (-1);
				}

				count = strtol(optarg, &cp, 0);
				if (!cp || *cp != '\0') {
					fprintf(stderr, "**** Invalid count %s\n", optarg);
					CMD_USAGE(ka);
					return (-1);
				}

				break;

			case 'a':
				if (HAVE_RANGE)  {
					fprintf(stderr, "**** -a can't be used with -[nsSeE]\n");
					CMD_USAGE(ka);
					return (-1);
				}

				all    = 1;
				starti = 1;  // all is obviously inclusive
				endi   = 1;
				break;

			case 's':
				if (starti || all) {
					fprintf(stderr, "**** only one of -[asS]\n");
					CMD_USAGE(ka);
					return (-1);
				}

				start = optarg;
				break;

			case 'S':
				if (start || all) {
					fprintf(stderr, "**** only one of -[asS]\n");
					CMD_USAGE(ka);
					return (-1);
				}

				starti = 1;
				start  = optarg;
				break;

			case 'e':
				if (end || all) {
					fprintf(stderr, "**** only one of -[aeE]\n");
					CMD_USAGE(ka);
					return (-1);
				}

				end = optarg;
				break;

			case 'E':
				if (endi || all) {
					fprintf(stderr, "**** only one of -[aeE]\n");
					CMD_USAGE(ka);
					return (-1);
				}

				end  = optarg;
				endi = 1;
				break;

			case 'h':
			case '?':
			default:
				CMD_USAGE(ka);
				return(-1);
		}
	}

	if (HAVE_RANGE && bat) {
		fprintf(stderr, "**** Warning: Batch Range Delete: ");
		fprintf(stderr,
			"Range must not contain more than %u keys\n",
			ka->ka_limits.kl_batdelcnt
		);
	}

	/* No reason to time the call is a user input is required */
	if (ka->ka_yes && ka->ka_stats)
		clock_gettime(CLOCK_MONOTONIC, &tstart);

	/* Init kv */
	if (!(kv = ki_create(ktd, KV_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	kv->kv_key     = kv_key;
	kv->kv_keycnt  = 1;
	kv->kv_val     = kv_val;
	kv->kv_valcnt  = 1;
	kv->kv_cpolicy = cpolicy;

	/*
	 * Check for the key parm,
	 * should only be present if no range params given
	 */
	if ((argc - optind == 1) && !HAVE_RANGE) {
		/*
		 * This is the single key delete case
		 *
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if no escape sequences are
		 * present this amounts to a str copy.
		 */
		void *decode_result = asciidecode(
			argv[optind],
			strlen(argv[optind]),
			(void **) &ka->ka_key,
			&ka->ka_keylen
		);

		if (!decode_result) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return (-1);
		}

		#if 0
		printf("%s\n", argv[optind]);
		printf("%lu\n", ka->ka_keylen);
		hexdump(ka->ka_key, ka->ka_keylen);
		#endif

		/*
		 * Check and hang the key
		 */
		if (ka->ka_keylen > ka->ka_limits.kl_keylen ||
			ka->ka_vallen > ka->ka_limits.kl_vallen) {
			fprintf(stderr, "*** Key and/or value too long\n");
			return(-1);
		}

		kv->kv_key[0].kiov_base = ka->ka_key;
		kv->kv_key[0].kiov_len  = ka->ka_keylen;

		/*
		 * Get the key to prove the it exists
		 * and to get the current version
		 */
		// TODO: oh damn, how to free this protobuf?
		krc = ki_getversion(ktd, kv);
		if (krc != K_OK) {
			fprintf(stderr, "%s: %s\n", ka->ka_cmdstr,  ki_error(krc));
			return (-1);
		}

		if (ka->ka_verbose) {
			printf("Compare & Delete: %s\n", cmpdel?"Enabled":"Disabled");
			printf("Current Version:  %s\n", (char *)kv->kv_ver);

		}

		if (ka->ka_yes) {
			printf("***DELETING Key: ");
		} else {
			printf("***DELETE? Key: ");
		}

		asciidump(ka->ka_key, ka->ka_keylen);
		printf("\n");

		/* ka_yes must be first to short circuit the conditional */
		if (ka->ka_yes || yorn("Please answer y or n [yN]: ", 0, 5)) {
			if (cmpdel)
				krc = ki_cad(ktd, (bat?ka->ka_batch:NULL), kv);
			else
				krc = ki_del(ktd, (bat?ka->ka_batch:NULL), kv);

			// call clean so that the kv protobuf is destroyed
			// TODO: currently hoping `destroy_command` is idempotent
			// ki_clean(kv);

			if (krc != K_OK) {
				fprintf(stderr,
					"%s: Unable to delete key: %s\n",
					ka->ka_cmdstr,  ki_error(krc));
				return(-1);
			}
		}

		if (flush) {
			if (ka->ka_verbose)
				fprintf(stderr, "%s: Flushing\n", ka->ka_cmdstr);

			krc = ki_flush(ktd);
			if (krc != K_OK) {
				fprintf(stderr, "%s: Flush failed: %s\n",
					ka->ka_cmdstr,  ki_error(krc));
				return (-1);
			}
		}

		if (ka->ka_yes && ka->ka_stats) {
			uint64_t t;
			clock_gettime(CLOCK_MONOTONIC, &tstop);
			ts_sub(&tstop, &tstart, t);
			printf("KCTL Stats: Del time: %lu \xC2\xB5S\n", t);
		}
	}

	else if ((argc - optind == 0) && (HAVE_RANGE)) {
		/*
		 * No key but a range defined, this is bueno.
		 *
		 * This is the range key delete, could be all, a key range, or
		 * a count limited key range.
		 *
		 * Setup the key range:
		 * Zero out the kr structure
		 * If all is set leave kr.kr_start and kr.kr_end unset
		 * If no start key provided, leave kr.kr_start unset
		 * If no end key provided, leave kr.kr_end unset
		 */
		if (!(kr = ki_create(ktd, KRANGE_T))) {
			fprintf(stderr, "*** Memory Failure\n");
			return (-1);
		}

		if (start) {
			kr->kr_start		= startkey;
			kr->kr_startcnt		= 1;

			/*
			 * Aways decode any ascii arbitrary hexadecimal value
			 * escape sequences in the passed-in key, if no escape
			 * sequences are present this amounts to a str copy.
			 */
			void *decode_result = asciidecode(
				start,
				strlen(start),
				&kr->kr_start[0].kiov_base,
				&kr->kr_start[0].kiov_len
			);

			if (!decode_result) {
				fprintf(stderr, "*** Failed start key conversion\n");
				CMD_USAGE(ka);
				return (-1);
			}
		}

		/* set the inclusive flag */
		if (starti) {
			KR_FLAG_SET(kr, KRF_ISTART);
		}

		if (end) {
			kr->kr_end	  = endkey;
			kr->kr_endcnt = 1;

			/*
			 * Aways decode any ascii arbitrary hexadecimal value
			 * escape sequences in the passed-in key, if no escape
			 * sequences are present this amounts to a str copy.
			 */
			void *decode_result = asciidecode(
				end,
				strlen(end),
				&kr->kr_end[0].kiov_base,
				&kr->kr_end[0].kiov_len
			);

			if (!decode_result) {
				fprintf(stderr, "*** Failed end key conversion\n");
				CMD_USAGE(ka);
				return (-1);
			}
		}

		/* set the inclusive flag */
		if (endi) {
			KR_FLAG_SET(kr, KRF_IEND);
		}

		kr->kr_count = ((count < 0) ? KVR_COUNT_INF : count);

		/* go ahead and ask the question or if ka_yes tell */
		printf("%s ", (ka->ka_yes) ? "***DELETING" : "***DELETE");

		/* Print what is to be deleted in range form.
		 * Keys can be large so just print first 5 chars of
		 * each key defining the range. Print the count as well.
		 * Use range notation for start, end:
		 * 	[ or ] = inclusive of the element,
		 * 	( or ) = exclusive of the element
		 */
		if (all) {
			printf("All Keys: [{FIRSTKEY}, {LASTKEY}]:unlimited");
		}
		else {
			printf("Key Range %s", starti?"[":"(");
			if (!start)
				printf("{FIRSTKEY}");
			else {
				int l;
				l = strlen(start);
				asciidump(start, (l>5)?5:l);
			}

			printf(",");

			if (!end) {
				printf("{LASTKEY}");
			}
			else {
				int l = strlen(end);
				asciidump(end, (l > 5) ? 5 : l);
			}

			printf("%s:", endi?"]":")");

			if (count > 0) { printf("%u", count); }
			else           { printf("unlimited"); }
		}

		/* ka_yes must be first to short circuit the conditional */
		if (!ka->ka_yes && !(yorn("?\nPlease answer y or n [yN]: ", 0, 5))) {
			return (0);
		} else {
			printf("\n");
		}

		/******* Green Light - bulk deleting from here *****/

		/* Create the kinetic range iterator */
		if (!(kit = ki_create(ktd, KITER_T))) {
			fprintf(stderr, "*** Memory Failure\n");
			return (-1);
		}

		/* Iterate */
		for (k = ki_start(kit, kr); k; k = ki_next(kit)) {
			dels++;
			if (bat && (dels > ka->ka_limits.kl_batdelcnt)) {
				fprintf(stderr,
					"%s: Range too big for batch: %u\n",
					ka->ka_cmdstr,
					ka->ka_limits.kl_batdelcnt);

				ki_destroy(kit);
				ki_destroy(kr);

				return (-1);
			}

			/* Set the key */
			kv->kv_key[0].kiov_base = k->kiov_base;
			kv->kv_key[0].kiov_len  = k->kiov_len;

			if (cmpdel) {
				/*
				 * Get the key's the current version
				 */
				krc = ki_getversion(ktd, kv);
				if (krc != K_OK) {
					fprintf(stderr, "%s: %s\n", ka->ka_cmdstr, ki_error(krc));
					return (-1);
				}

				krc = ki_cad(ktd, (bat?ka->ka_batch:NULL), kv);
			}

			else {
				krc = ki_del(ktd, (bat?ka->ka_batch:NULL), kv);
			}

			if (krc != K_OK) {
				asciidump(k->kiov_base,  k->kiov_len);
				fprintf(stderr,
					"%s: Unable to delete key: %x: %s\n",
					ka->ka_cmdstr,
					krc, ki_error(krc));

				ki_destroy(kit);
				ki_destroy(kr);

				return (-1);
			}
		}

		if (flush) {
			if (ka->ka_verbose)
				fprintf(stderr, "%s: Flushing\n", ka->ka_cmdstr);

			krc = ki_flush(ktd);
			if (krc != K_OK) {
				fprintf(stderr, "%s: Flush failed: %s\n", ka->ka_cmdstr,  ki_error(krc));
				return (-1);
			}
		}

		if (ka->ka_stats) {
			uint64_t t;
			double   m;

			clock_gettime(CLOCK_MONOTONIC, &tstop);
			ts_sub(&tstop, &tstart, t);
			m = (double) t / (double) dels;

			printf(
				"KCTL Stats: Del time, mean: %10.10g \xC2\xB5S (n=%d) %10.10g dels/S\n",
				m, dels, (dels / (t / ((double)1000000.0)))
			);
		}

		if (ka->ka_verbose)
			fprintf(stdout, "%s: Deleted %d keys\n", ka->ka_cmdstr, dels);

		ki_destroy(kit);
		ki_destroy(kr);
	}

	/* A Key and a Range, no bueno. */
	else if ((argc - optind == 1) && (HAVE_RANGE)) {
		fprintf(stderr, "**** Key provided with range arguments defined\n");
		CMD_USAGE(ka);
		return (-1);
	}

	/* No Key and No range, no bueno */
	else  {
		fprintf(stderr, "**** No key and no range provided\n");
		CMD_USAGE(ka);
		return (-1);
	}

	ki_destroy(kv);
	return (0);
}

