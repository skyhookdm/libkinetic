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
#include <sys/stat.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define VERLEN 11

/* used to dumpprint cache policy */
extern char *ki_cpolicy_label[];
extern char *ki_status_label[];

#define CMD_USAGE(_ka) kctl_put_usage(_ka)

void
kctl_put_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY VALUE\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-b           Add to current batch [no]\n");
	fprintf(stderr, "\t-c           Compare and swap [no]\n");
	fprintf(stderr, "\t-f filename  Construct a value from a file\n");
	fprintf(stderr, "\t-z len       Construct a length len value of 0s\n");
	fprintf(stderr, "\t-p [wt|wb|f] Cache policy:\n");
	fprintf(stderr, "\t             writethrough, writeback, flush [wb]\n");
	fprintf(stderr, "\t-s sum       Value CRC32 sum (8 hex digits) [0] \n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nWhere, KEY and VALUE are quoted strings that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

/*
 * Put a Key Value pair
 * By default kctl stores the version number as 8 digit hexidecimal ascii 
 * number starting at "0x00000000" for new keys.
 * By default versions are ignored, compare and swap can be enabled with -c
 * This enforces the correct version is passed to the put or else it fails
 * All persistence modes are supported with -p [wt,wb,f] defaulting to 
 * Write Back.
 * A CRC32 check sum of the value can be stored -s sum
 */
int
kctl_put(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *cp, *filename=NULL;
	int 		cmpswp=0, exists=0, zlen=0, bat=0, fd;
	uint32_t	sum = 0;
	struct stat	st;
	char		newver[VERLEN]; 	// holds hex representation of
						// one int: "0x00000000"
	kcachepolicy_t	cpolicy = KC_WB;
	kv_t		*kv;
	struct kiovec	kv_key[1]  = {{0, 0}};
	struct kiovec	kv_val[1]  = {{0, 0}};
	kstatus_t 	krc;

        while ((c = getopt(argc, argv, "bcf:h?p:s:z:")) != EOF) {
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
			cmpswp = 1;
		case 'f':
			filename = optarg;
			if (zlen) {
				fprintf(stderr, "**** No -z and -f\n");
				CMD_USAGE(ka);
				return(-1);
			}				
			break;
		case 'p':
			if (strlen(optarg) > 2) {
				fprintf(stderr, "**** Bad -p flag option %s\n",
					optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			if (strncmp(optarg, "wt", 2) == 0)
				cpolicy = KC_WT;
			else if (strncmp(optarg, "wb", 2) == 0)
				cpolicy = KC_WB;
			else if (strncmp(optarg, "f", 1) == 0)
				cpolicy = KC_FLUSH;
			else {
				fprintf(stderr, "**** Bad -p flag option: %s\n",
					optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'z':
			zlen = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "*** Invalid zlen %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			
			if (zlen > ka->ka_limits.kl_vallen) {
				fprintf(stderr, "*** zlen too long (%d > %d)\n",
					zlen, ka->ka_limits.kl_vallen);
				return(-1);
			}

			if (filename) {
				fprintf(stderr, "**** No -f and -z\n");
				CMD_USAGE(ka);
				return(-1);
			}				
			break;
		case 's':
			if (strlen(optarg) > 8) {
				CMD_USAGE(ka);
				return(-1);
			}
			sum = (uint32_t)strtoul(optarg, NULL, 16);
			break;
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	// Check for the key and value parms
	if (argc - optind == 2) {
		/* read value buffer from the command line */
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ka->ka_key, &ka->ka_keylen)) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
		optind++;
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ka->ka_val, &ka->ka_vallen)) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
		
	} else if (argc - optind == 1 && zlen) {
		/* Construct zero value buffer of length zlen */
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ka->ka_key, &ka->ka_keylen)) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
		optind++;
		ka->ka_val = (char *)malloc(zlen);
		if (!ka->ka_val) {
			fprintf(stderr, "*** Unable to alloc zlen buffer\n");
			return(-1);
		}
		ka->ka_vallen = zlen;
		memset(ka->ka_val, 0, zlen);
		memcpy(ka->ka_val, ka->ka_key, ka->ka_keylen); /* tag it */
	} else if (argc - optind == 1 && filename) {
		/* Construct value buffer from file contents */
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ka->ka_key, &ka->ka_keylen)) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
		optind++;

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
		
		ka->ka_vallen	= st.st_size;
		ka->ka_val	= (char *)malloc(ka->ka_vallen);
		if (!ka->ka_val) {
			fprintf(stderr, "*** Unable to alloc %lu bytes\n",
				ka->ka_vallen);
			CMD_USAGE(ka);
			return(-1);
		}
		
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "*** Unable to open file %s\n",
				filename);
			CMD_USAGE(ka);
			return(-1);
		}
		
		if (read(fd, ka->ka_val, ka->ka_vallen) != ka->ka_vallen){
			fprintf(stderr, "*** Unable to read file %s\n",
				filename);
			CMD_USAGE(ka);
			return(-1);
		}

		close(fd);
	} else {
		fprintf(stderr, "*** Too few or too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	/* Init kv */
	if (!(kv = ki_create(ktd, KV_T))) {
		fprintf(stderr, "*** Memory Failure\n");
		return (-1);
	}

	kv->kv_key    = kv_key;
	kv->kv_keycnt = 1;
	kv->kv_val    = kv_val;
	kv->kv_valcnt = 1;
	
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

	/* Get the key's version if it exists and then increment the version */
	krc = ki_getversion(ktd, kv);
 	if(krc == K_OK) {
		unsigned long nv;
		nv = strtoul(kv->kv_ver, NULL, 16) + 1; /* increment the vers */
		sprintf(newver, "0x%08x", (unsigned int)nv);
		exists = 1;
	} else { 
		/*
		 * Get failure likely means no existing key so no version
		 * use default 0
		 */
		sprintf(newver, "0x%08x", 0);
	}
	
	kv->kv_newver = newver;
	kv->kv_newverlen = VERLEN;
	kv->kv_disum = &sum;
	kv->kv_disumlen = sizeof(sum);
	kv->kv_ditype = KDI_CRC32;
	kv->kv_cpolicy = cpolicy;

	/* Hang the value */
	kv->kv_val[0].kiov_base = ka->ka_val;
	kv->kv_val[0].kiov_len  = ka->ka_vallen;

	if (ka->ka_verbose) {
		printf("Compare & Swap:  %s\n", cmpswp?"Enabled":"Disabled");
		printf("Version:         %s\n", exists?(char *)kv->kv_ver:"");
		printf("New Version:     %s\n", (char *)kv->kv_newver);
		printf("DI Sum:          %08x\n", *(uint32_t *)kv->kv_disum);
		printf("DI Type:         CRC32\n");
		printf("Cache Policy:    %s\n", 
		       ki_cpolicy_label[kv->kv_cpolicy]);
	}
	
        if (cmpswp)
		krc = ki_cas(ktd, (bat?ka->ka_batch:NULL), kv);
	else
		krc = ki_put(ktd, (bat?ka->ka_batch:NULL), kv);
	
	if (krc != K_OK) {
		fprintf(stderr, "%s: %s: %s\n",
			ka->ka_cmdstr, ka->ka_key, ki_error(krc));
		return(-1);
	}

	ki_destroy(kv);
	return(0);
}


