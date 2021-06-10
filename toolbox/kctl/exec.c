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

#define CMD_USAGE(_ka) kctl_exec_usage(_ka)
#define MAX_COUNT 1000
#define MAX_DIGITS 4		/* Max digits in MAX_COUNT */
typedef char suffix_t[MAX_DIGITS+2];	/* + 2 for '.' and '\0' */


void
kctl_exec_usage(struct kargs *ka)
{
        fprintf(stderr,
		"Usage: %s [..] %s [CMD OPTIONS] <FN KEY> [ARGS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nExecute the function stored in FN KEY\n");
	fprintf(stderr, "Where, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-n count     Number of fn keys\n");
	fprintf(stderr, "\t-g key       Get key as part of the result\n");
	fprintf(stderr, "\t-t [native | llvm | java | ebpf]\n");
	fprintf(stderr, "\t-A           Dumps key/value as ascii w/escape seqs\n");
	fprintf(stderr, "\t-X           Dumps key/value as both hex and ascii\n");
	fprintf(stderr, "\t             Function type [native]\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "Where, FN KEY is the key of the function to be executed.\n");
	fprintf(stderr, "FN KEY is a string that can contain ASCII chars and arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nIf -n count is provided, then FN KEY is used as a basekey\n");
	fprintf(stderr, "count times with the copies suffixed with an index, 0 through [count - 1]\n");
	fprintf(stderr, "The suffix has a leading '.' and is zero padded with a length of %d.\n", MAX_DIGITS);
	fprintf(stderr, "ex kctl exec -n 2 myfunc yields myfunc.%0*d and myfunc.%0*d \n", MAX_DIGITS, 0, MAX_DIGITS, 1);
	fprintf(stderr, "The values from these constructed keys will be concatenated in\n");
	fprintf(stderr, "the order provided to create a single executable function\n");
	

	fprintf(stderr, "\nARGS is a space separated list of arguments which are\n");
	fprintf(stderr, "passed unaltered to the FN.\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}


/* Execute a function on the kinetic server */
int
kctl_exec(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
	int		i, hdump = 0, adump = 0;
        char		c, *cp;
	uint32_t	count = 0;
	char 		*gkey = NULL;
	size_t		gkeylen;
	int 		fnargc = 0;
	char 		**fnargv = NULL;
	suffix_t	*suffix;
	struct kiovec	*kv_key;
	struct kiovec	kv_gkey;
	struct kiovec	kv_gval = {0, 0};
	kv_t		**fnkv,  *gkv = NULL;
	kfn_t		fntype = KF_NATIVE;
	char 		*fntypestr = "native";
	kapplet_t	*app;
	kstatus_t	krc;
	
        while ((c = getopt(argc, argv, "h?n:g:t:AX")) != EOF) {
                switch (c) {
		case 'g':
			if (!asciidecode(optarg, strlen(optarg),
					 (void **)&gkey, &gkeylen)) {
				fprintf(stderr,
					"*** Failed get key conversion\n");
				CMD_USAGE(ka);
				return(-1);
			}
			break;
			
		case 'n':
			if (optarg[0] == '-') {
				fprintf(stderr, "*** Negative count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}

			count = strtoul(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "**** Invalid count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			if (count > MAX_COUNT) {
				fprintf(stderr, "**** Count too large %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;

		case 't':
			if (!strcmp("native", optarg))
				fntype = KF_NATIVE;
			else if (!strcmp("llvm", optarg))
				fntype = KF_LLVMIR;
			else if (!strcmp("java", optarg))
				fntype = KF_JAVA;
			else if (!strcmp("ebpf", optarg))
				fntype = KF_EBPF;
			else {
				fprintf(stderr, "*** No such FN type %s\n",
					optarg);
				CMD_USAGE(ka);
			}
			fntypestr = optarg;
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
			return(-1);
		}
        }

	/* Check for the fn key and optional fn arguments */
	if (argc - optind > 0) {
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ka->ka_key, &ka->ka_keylen)) {
			fprintf(stderr, "*** Failed fn key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
		optind++;
		
		/* Grab any arguments */
		if  (optind < argc) {
			fnargv = &argv[optind];
			fnargc = argc - optind;
		}
	} else {
		fprintf(stderr, "*** Too few or too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	if (count) {
		/* Need count kv ptrs. */
		fnkv = (kv_t **)malloc(sizeof(kv_t *) * count);
		
		/* Need a suffix per key */
		suffix = (suffix_t *)malloc(sizeof(suffix_t) * count);

		/* Need 2 kiovecs per key: basekey + suffix */
		kv_key = (struct kiovec *)malloc(sizeof(struct kiovec) * 2 * count);

		if (!fnkv || !suffix || !kv_key) {
			fprintf(stderr, "*** KV, suffix or kiovec allocation failure\n");
			return(-1);
		}

		for (i=0; i<count; i++) {
			/* Create the suffix string */
			sprintf(suffix[i], ".%0*d", MAX_DIGITS, i);

			/* Hang the basekey and suffix on the kiov */
			kv_key[(i*2)].kiov_base   = ka->ka_key;
			kv_key[(i*2)].kiov_len    = ka->ka_keylen;
			kv_key[(i*2)+1].kiov_base = &suffix[i];
			kv_key[(i*2)+1].kiov_len  = sizeof(suffix_t) - 1;

		       	if (!(fnkv[i] = ki_create(ktd, KV_T))) {
				fprintf(stderr, "*** KV create failure\n");
				return (-1);
			}

			/* Hang the kiov on the kv */
			fnkv[i]->kv_key    = &kv_key[i*2];
			fnkv[i]->kv_keycnt = 2;
		}

	} else {
		/* This is the single FN KEY case */ 
		fnkv = (kv_t **)malloc(sizeof(kv_t *));

		/* Need 1 kiovecs */
		kv_key = (struct kiovec *)malloc(sizeof(struct kiovec));

		if (!fnkv || !kv_key) {
			fprintf(stderr, "*** KV or kiovec allocation failure\n");
			return(-1);
		}

		kv_key->kiov_base   = ka->ka_key;
		kv_key->kiov_len    = ka->ka_keylen;

		if (!(fnkv[0] = ki_create(ktd, KV_T))) {
			fprintf(stderr, "*** KV create failure\n");
			return (-1);
		}

		/* Hang the kiov on the kv */
		fnkv[0]->kv_key    = kv_key;
		fnkv[0]->kv_keycnt = 1;
		count = 1;
	}

	/* Setup the get key if required */
	if (gkey) {
		kv_gkey.kiov_base = gkey;
		kv_gkey.kiov_len  = gkeylen;

		if (!(gkv = ki_create(ktd, KV_T))) {
			fprintf(stderr, "*** Get KV create failure\n");
			return (-1);
		}
		
		/* Hang the key kiov on the kv */
		gkv->kv_key    = &kv_gkey;
		gkv->kv_keycnt = 1;

		/* Hang the empty val kiov on the kv */
		gkv->kv_val    = &kv_gval;
		gkv->kv_valcnt = 1;
}

	/* Now setup the applet */
	if (!(app = ki_create(ktd, KAPPLET_T))) {
		fprintf(stderr, "*** Kapplet create failure\n");
		return (-1);
	}
		
	app->ka_fnkey  = fnkv;
	app->ka_fnkeys = count;
	app->ka_fntype = fntype;
	app->ka_argv   = fnargv;
	app->ka_argc   = fnargc;
	app->ka_outkey = gkv;

	if (ka->ka_verbose) {
		printf("Executing function: %s\n", ka->ka_key);
		printf("Fnkey count: %d\n", count);
		if (suffix) {
			printf("Sharded keys:\n");
			for (i=0; i<count; i++)
				printf("\t\t%s%s\n", ka->ka_key, suffix[i]);
		}
		printf("Fnkey type: %s\n", fntypestr);
		printf("Fnkey args: ");
		for (i=0; i<fnargc; i++)
			printf("%s ", fnargv[i]);
		printf("\n");
		if (gkey) {
			printf("Returning key: %s\n", gkey);
		}
		
	}
	
	krc = ki_exec(ktd, app);
	if (krc != K_OK) {
		fprintf(stderr, "%s: %s: %s\n",
			ka->ka_cmdstr, ka->ka_key, ki_error(krc));
		return(-1);
	}

	if (gkey) {
		if (!ka->ka_quiet) {
			/* Print key length */
			printf("Result KV\nLength: %lu\n", gkv->kv_val[0].kiov_len);
		}

		if (adump) {
			asciidump(gkv->kv_val[0].kiov_base, gkv->kv_val[0].kiov_len);
		} else if (hdump) {
			hexdump(gkv->kv_val[0].kiov_base, gkv->kv_val[0].kiov_len);
		} else {
			/* raw */
			write(fileno(stdout),
			      gkv->kv_val[0].kiov_base, gkv->kv_val[0].kiov_len);
		}

		if (!ka->ka_quiet) {
			printf("\n");
		}
	}

	if (!ka->ka_quiet) {
		printf("Exit Code  : %d\n", app->ka_rc);
		printf("Exit Signal: %d\n", app->ka_sig);
		printf("Exit Mesg  : %s\n", app->ka_msg);
		printf("Std Out    : %s\n", app->ka_stdout);
	}
	
	return(app->ka_rc);
}
