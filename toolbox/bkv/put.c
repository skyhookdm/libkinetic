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
#include <sys/stat.h>

#include <kinetic/basickv.h>
#include "bkv.h"

#define CMD_USAGE(_ka) b_put_usage(_ka)

void
b_put_usage(struct bargs *ba)
{
	fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY VALUE\n",
		ba->ba_progname,ba->ba_cmdstr);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-f filename  Construct a value from a file\n");
	fprintf(stderr, "\t-l           Put a large value spanning keys\n");
	fprintf(stderr, "\t-z len       Construct a length len value of 0s\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nWhere, KEY and VALUE are quoted strings that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nFor -l, the value length to be stored will not fit into a\n");
	fprintf(stderr, "single key. KEY is the base key and keys are generated by appending\n");
	fprintf(stderr, "an increasing sequence number to KEY until the entire value is\n");
	fprintf(stderr, "stored. Ex. if KEY is \"mykey\", then the first key stored is\n");
	fprintf(stderr, "\"mykey.000\". Used in conjunction with get -n <count>.\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");

}

int
b_put(int argc, char *argv[], int ktd, struct bargs *ba)
{
	extern char	*optarg;
	extern int	optind, opterr, optopt;
	struct stat	st;
	int		rc, fd, large = 0;
	uint64_t	zlen = 0, maxlen;
	uint32_t	count = 0;
	char		c, *cp, *rs, *filename = NULL;

	while ((c = getopt(argc, argv, "f:h?lz:")) != EOF) {
		switch (c) {
		case 'f':
			filename = optarg;
			if (zlen) {
				fprintf(stderr, "**** No -z and -f\n");
				CMD_USAGE(ba);
				return(-1);
			}				
			break;
			
		case 'l':
			/* Put a large value */
			large = 1;
			break;
			
		case 'z':
			if (optarg[0] == '-') {
				fprintf(stderr, "*** Negative zlen %s\n",
				       optarg);
				CMD_USAGE(ba);
				return(-1);
			}
			zlen = strtoul(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "*** Invalid zlen %s\n",
				       optarg);
				CMD_USAGE(ba);
				return(-1);
			}
			
			if (filename) {
				fprintf(stderr, "**** No -f and -z\n");
				CMD_USAGE(ba);
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

	/*
	 * A key always has to be on the command line, but there 
	 * can be a value as well.  So this covers the too few case 
	 */
	if (argc - optind < 1) {
		fprintf(stderr, "*** Too few args\n");
		CMD_USAGE(ba);
		return (-1);
	}
	
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
	optind++;

	/* Calculate the maxlen based on large flag. */
	maxlen = ba->ba_limits.bkvl_vlen * (large?ba->ba_limits.bkvl_maxn:1);
	
	/* 
	 * Already checked for too few args and incremented optind.
	 * Now there may be a value on the command line or we may 
	 * generate the value. Can't do pass a value and gen a value 
	 * at the same time (first clause) and there shouldn't ever
	 * be more than a value left on the cmd line (second clause)
	 */
	if (((zlen || filename ) && (argc - optind == 1)) ||
	     (argc - optind > 1)) {
				fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ba);
		return (-1);
	}

	if (argc - optind == 1) {
		/* Value on the command line */
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ba->ba_val, &ba->ba_vallen)) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ba);
			return(-1);
		}
		
		if (ba->ba_vallen > maxlen) {
			fprintf(stderr, "*** value too long (%lu > %lu)\n",
				ba->ba_vallen, maxlen);
			return(-1);
		}


	} else if (zlen) {
		if (zlen > maxlen) {
			fprintf(stderr, "*** zlen too long (%lu > %ld)\n",
				zlen, ba->ba_limits.bkvl_vlen);
			return(-1);
		}

		/* Generate zero filled value buffer */
		ba->ba_val = (char *)malloc(zlen);
		if (!ba->ba_val) {
			fprintf(stderr, "*** Unable to alloc zlen buffer\n");
			return(-1);
		}
		ba->ba_vallen = zlen;
		memset(ba->ba_val, 0, zlen);
		memcpy(ba->ba_val, ba->ba_key, ba->ba_keylen); /* tag it */
		
	} else if (filename) {
		/* Generate file filled value buffer */
		if (stat(filename, &st) < 0) {
			perror("stat");
			fprintf(stderr, "*** Error accessing file %s\n",
				filename);
			CMD_USAGE(ba);
			return(-1);
		}

		if (st.st_size > (size_t)maxlen) {
			fprintf(stderr, "*** file too long (%lu > %lu)\n",
				st.st_size, ba->ba_limits.bkvl_vlen);
			return(-1);
		}
		
		ba->ba_vallen	= st.st_size;
		ba->ba_val	= (char *)malloc(ba->ba_vallen);
		if (!ba->ba_val) {
			fprintf(stderr, "*** Unable to alloc %lu bytes\n",
				ba->ba_vallen);
			CMD_USAGE(ba);
			return(-1);
		}
		
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "*** Unable to open file %s\n",
				filename);
			CMD_USAGE(ba);
			return(-1);
		}
		
		if (read(fd, ba->ba_val, ba->ba_vallen) != ba->ba_vallen){
			fprintf(stderr, "*** Unable to read file %s\n",
				filename);
			CMD_USAGE(ba);
			return(-1);
		}

		close(fd);
		
	} else {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ba);
		return (-1);
	}
#if 0
	printf("Putting key:\n");
	printf("%lu\n", ba->ba_keylen);
	hexdump(ba->ba_key, ba->ba_keylen);
	printf("Value:\n");
	printf("%lu\n", ba->ba_vallen);
	hexdump(ba->ba_val, ba->ba_vallen);
#endif

	/* Put the key */
	if (large) {
		rc = bkv_putn(ktd, ba->ba_key, ba->ba_keylen, &count,
			     ba->ba_val, ba->ba_vallen);
		if (!ba->ba_quiet)
			printf("Put %d keys\n", count );
	} else {
		rc = bkv_put(ktd, ba->ba_key, ba->ba_keylen,
			     ba->ba_val, ba->ba_vallen);
	}

	if (rc < 0) {
		printf("%s: Put failed.\n", ba->ba_cmdstr);
		return(rc);
	}

	return(rc);
}

