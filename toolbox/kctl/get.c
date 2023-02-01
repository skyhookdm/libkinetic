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
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define CMD_USAGE(_ka) kctl_get_usage(_ka)

void
kctl_get_usage(struct kargs *ka)
{
	fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ka->ka_progname, ka->ka_cmdstr);

	char msg[] = "\n\
Where, CMD OPTIONS are [default]:\n\
	-m           Get only the metadata\n\
	-A           Dumps key/value as ascii w/escape seqs\n\
	-X           Dumps key/value as both hex and ascii\n\
	-?           Help\n\
\n\
Where, KEY is a quoted string that can contain arbitrary\n\
hexidecimal escape sequences to encode binary characters.\n\
Only \\xHH escape sequences are converted, ex \\xF8.\n\
If a conversion fails the command terminates.\n\
\n\
By default keys and values are printed as raw strings,\n\
including special/nonprintable chars\n\
\n\
To see available COMMON OPTIONS: ./kctl -?\n";

	fprintf(stderr, "%s", msg);
}

extern const char *ki_ditype_label[];
extern const int ki_ditype_max;

const char *
kctl_ditype_str(int d)
{
	if ((d > 0) && (d <= ki_ditype_max))
		return (ki_ditype_label[d]);
	else
		return (ki_ditype_label[0]);
}


int
kctl_get(int argc, char *argv[], int ktd, struct kargs *ka)
{
	extern char   *optarg;
	extern int     optind, opterr, optopt;
	char           c, *rkey;
	int            hdump = 0, adump = 0, meta = 0, kctl_status = 0;

	kv_t           *kv;
	struct kiovec  kv_key[1]  = {{0, 0}};
	struct kiovec  kv_val[1]  = {{0, 0}};

	kv_t           *rkv;
	struct kiovec  rkv_key[1] = {{0, 0}};
	struct kiovec  rkv_val[1] = {{0, 0}};

	kstatus_t      krc;
	kv_t          *pkv;

	struct timespec start, stop;

	// This while loop can return early because there are no allocs to manage
	while ((c = getopt(argc, argv, "mAXh?")) != (char)EOF) {
		switch (c) {
		case 'm':
			meta = 1;
			break;
			
		case 'A':
			adump = 1;
			if (hdump) {
				fprintf(stderr,
					"*** -X and -A are exclusive\n");
				CMD_USAGE(ka);
				return (-1);
			}
			break;

		case 'X':
			hdump = 1;
			if (adump) {
				fprintf(stderr,
					"*** -X and -A are exclusive\n");
				CMD_USAGE(ka);
				return (-1);
			}
			break;

		case 'h':
		case '?':
		default:
			CMD_USAGE(ka);
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
		void *decoded_data = asciidecode(
			 argv[optind]
			,strlen(argv[optind])
			,(void **) &ka->ka_key
			,          &ka->ka_keylen
		);

		if (!decoded_data) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return (-1);
		}
#if 0
		printf("%s\n", argv[optind]);
		printf("%lu\n", ka->ka_keylen);
		hexdump(ka->ka_key, ka->ka_keylen);
#endif
	}

	else {
		fprintf(stderr, "*** Too few or too many args\n");
		CMD_USAGE(ka);
		return (-1);
	}

	if (ka->ka_stats)
		clock_gettime(CLOCK_MONOTONIC, &start);

	/* Init kv and return kv */
	if (!(kv = ki_create(ktd, KV_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	if (!(rkv = ki_create(ktd, KV_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	kv->kv_key	= kv_key;
	kv->kv_keycnt	= 1;
	kv->kv_val	= kv_val;
	kv->kv_valcnt	= 1;
	kv->kv_metaonly	= meta;

	rkv->kv_key    = rkv_key;
	rkv->kv_keycnt = 1;
	rkv->kv_val    = rkv_val;
	rkv->kv_valcnt = 1;

	/*
	 * Hang the key
	 */
	kv->kv_key[0].kiov_base = ka->ka_key;
	kv->kv_key[0].kiov_len  = ka->ka_keylen;

	/*
	 * 4 cmd supported here: Get, GetNext, GetPrev, GetVers
	 * NOTE: all of these calls make allocations which need to be 
	 * cleaned before exit
	 */
	switch (ka->ka_cmd) {
	case KCTL_GET:
		pkv = kv; 
		krc = ki_get(ktd, kv);
		break;

	case KCTL_GETNEXT:
		krc = ki_getnext(ktd, kv, rkv);
		pkv = rkv;
		break;

	case KCTL_GETPREV:
		krc = ki_getprev(ktd, kv, rkv);
		pkv = rkv;
		break;

	case KCTL_GETVERS:
		krc = ki_getversion(ktd, kv);
		pkv = kv;
		break;

	default:
		// no allocation, so we can still return early
		fprintf(stderr, "Bad command: %s\n", ka->ka_cmdstr);
		return (-1);
	}

	switch (krc) {
	case K_OK:
		break;

	case K_ENOTFOUND:
		printf("%s: No key found.\n", ka->ka_cmdstr);

		kctl_status = (-1);
		goto kctl_gex;

	default:
		printf("%s: failed: status code %d %s\n",
		       ka->ka_cmdstr, krc, ki_error(krc));

		kctl_status = (-1);
		goto kctl_gex;
	}

	if (ka->ka_stats) {
		uint64_t t;
		clock_gettime(CLOCK_MONOTONIC, &stop);
		ts_sub(&stop, &start, t);
		printf("KCTL Stats: Get time: %lu \xC2\xB5S\n", t);
	}

	// ------------------------------
	// Print key name
	if (!ka->ka_quiet) {
		printf("Key(");
		if (adump) {
			asciidump(pkv->kv_key[0].kiov_base,
				  pkv->kv_key[0].kiov_len);
		}

		else if (hdump) {
			printf("\n");
			hexdump(pkv->kv_key[0].kiov_base,
				pkv->kv_key[0].kiov_len);
		} else {
			/* add null byte to print as a string */
			rkey = strndup((char *) pkv->kv_key[0].kiov_base,
				       pkv->kv_key[0].kiov_len);

			printf("%s", rkey);
			free(rkey);
		}
		printf(")\n");
	}

	// ------------------------------
	// Print key version
	if (ka->ka_cmd == KCTL_GETVERS) {
		if (!ka->ka_quiet) {
			printf("Version: ");
		}
		if (adump) {
			asciidump(pkv->kv_ver, pkv->kv_verlen);
			printf("\n");
		} else if (hdump) {
			hexdump(pkv->kv_ver, pkv->kv_verlen);
		} else {
		/* raw */
			printf("%s\n", kv->kv_ver?(char *)kv->kv_ver:"");
		}

		kctl_status = (-1);
		goto kctl_gex;
	}

	if (meta) {
		if (adump) {
			printf("Version:   ");
			asciidump(pkv->kv_ver, pkv->kv_verlen);
			printf("\nChecksum:  ");
			asciidump(pkv->kv_disum, pkv->kv_disumlen);
			printf("\nAlgorithm: ");
			printf("%s\n", kctl_ditype_str(pkv->kv_ditype));
		} else if (hdump) {
			printf("Version:   ");
			hexdump(pkv->kv_ver, pkv->kv_verlen);
			printf("Checksum:  ");
			hexdump(pkv->kv_disum, pkv->kv_disumlen);
			printf("Algorithm: ");
			printf("%s\n", kctl_ditype_str(pkv->kv_ditype));
		} else {
		/* raw */
			printf("Version:   ");
			hexdump(pkv->kv_ver, pkv->kv_verlen);
			printf("Checksum:  ");
			hexdump(pkv->kv_disum, pkv->kv_disumlen);
			printf("Algorithm: ");
			printf("%s\n", kctl_ditype_str(pkv->kv_ditype));
		}
		goto kctl_gex;
	}

	// ------------------------------
	// Print key length
	if (!ka->ka_quiet) {
		printf("\nLength: %lu\n", pkv->kv_val[0].kiov_len);
	}

	if (adump) {
		asciidump(pkv->kv_val[0].kiov_base, pkv->kv_val[0].kiov_len);
	} else if (hdump) {
		hexdump(pkv->kv_val[0].kiov_base, pkv->kv_val[0].kiov_len);
	} else {
		/* raw */
		write(fileno(stdout),
		      pkv->kv_val[0].kiov_base, pkv->kv_val[0].kiov_len);
	}

	if (!ka->ka_quiet) {
		printf("\n");
	}

 kctl_gex:

	ki_destroy((void *)kv);
	ki_destroy((void *)rkv);

	return kctl_status;
}

