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

int
kctl_range_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-n count	Number of keys [unlimited]\n");       
	fprintf(stderr, "\t-s KEY       Start Key in the range, non inclusive\n");
	fprintf(stderr, "\t-S KEY       Start Key in the range, inclusive\n");
	fprintf(stderr, "\t-e KEY       End Key in the range, non inclusive\n");
	fprintf(stderr, "\t-E KEY       End Key in the range, inclusive\n");
	fprintf(stderr, "\t-r           Reverse the order\n");
	fprintf(stderr, "\t-A           Show keys as encoded ascii strings\n");
	fprintf(stderr, "\t-X           Show keys as hex and ascii\n");
	fprintf(stderr, "\t-?           Help\n");

	// R"foo(....)foo" is a non escape char evaluated string literal 
	fprintf(stderr, "\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
kctl_range(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c, *cp;
	char 		*start = NULL, *end = NULL;
	int 		starti = 0, endi = 0;
        int 		count = KVR_COUNT_INF;
	int		reverse = 0;
	int		adump = 0, hdump = 0;
	kstatus_t 	kstatus;
	krange_t	kr;
	struct kiovec	startkey[1] = {0, 0};
	struct kiovec	endkey[1] = {0, 0};

        while ((c = getopt(argc, argv, "rs:S:e:E:n:AXh?")) != EOF) {
                switch (c) {
		case 'r':
			reverse = 1;
			break;
		case 'n':
			count = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0' || count==0) {
				fprintf(stderr, "*** Invalid count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 's':
			if (starti) {
				fprintf(stderr, "only one of -[sS]\n");
				CMD_USAGE(ka);
				return(-1);
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
			start = optarg;
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
			end = optarg;
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

	/*
	 * Setup the range
	 * If no start key provided, set kr_start to NULL
	 * If no end key provided, set kr_end to NULL 
	 */
	memset(&kr, 0, sizeof(kr));

	if (start) {
		kr.kr_start		= startkey;
		kr.kr_startcnt		= 1;

		// Aways decode any ascii arbitrary hexadecimal value escape
		// sequences in the passed-in key, if no escape sequences are
		// present this amounts to a str copy.
		if (!asciidecode(start, strlen(start),
				 &kr.kr_start[0].kiov_base,
				 &kr.kr_start[0].kiov_len)) {
			fprintf(stderr, "*** Failed start key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
	}

	if (starti) {
		KR_FLAG_SET(&kr, KRF_ISTART);
	}
	
	if (end) {
		kr.kr_end		= endkey;
		kr.kr_endcnt		= 1;

		// Aways decode any ascii arbitrary hexadecimal value escape
		// sequences in the passed-in key, if no escape sequences are
		// present this amounts to a str copy.
		if (!asciidecode(end, strlen(end),
				 &kr.kr_end[0].kiov_base,
				 &kr.kr_end[0].kiov_len)) {
			fprintf(stderr, "*** Failed end key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
	}
	
	if (endi) {
		KR_FLAG_SET(&kr, KRF_IEND);
	}

	if (reverse) {
		KR_FLAG_SET(&kr, KRF_REVERSE);
	}

	kr.kr_count = count;

	// If verbose dump the range we are acting on
	// Print first 5 chars of each key defining the range
	if (ka->ka_verbose)  {
		printf("Keys [");
		if (!start && !starti )
			printf("{START}");
		else
			if (adump)
				asciidump(start, 5);
			else
				printf("%.5s", start);
		
		printf(":");
		
		if (!end && !endi )
			printf("{END}");
		else
			if (adump)
				asciidump(end,5);
			else
				printf("%.5s", end);
		printf(":");

		
		if (count > 0)
			printf("%u]\n", count);
		else
			printf("unlimited]\n");
	}

	// Iterate over all the keys and print them out

	// If 0 < count <= GETKEYRANGE_MAX_COUNT (count=-1 is unlimited)
	// then use a single GetKeyRange call, no need to iterate.
	// Of course this is unnecessary but allows the caller to test
	// GetKeyRange directly without going through the IterateKeyRange code.
	if ((count > 0) && (count <= ka->ka_limits.kl_rangekeycnt)) {
		kstatus = ki_range(ktd, &kr);
		if(kstatus.ks_code != K_OK) {
			fprintf(stderr,
				"%s: Unable to get key range: %s\n",
				ka->ka_cmdstr, kstatus.ks_message);
			return(-1);
		}

		if (ka->ka_verbose) printf("GKR\n");

		if (!kr.kr_keyscnt) {
			printf("No Keys Found.\n");
			return(0);
		}
		
		for(int i=0; i<kr.kr_keyscnt; i++) {
			if (ka->ka_verbose)
				printf("%u: ", i);
			if (hdump)
				hexdump((char *)kr.kr_keys[i].kiov_base,
					kr.kr_keys[i].kiov_len);
			else if (adump)
			        asciidump((char *)kr.kr_keys[i].kiov_base,
					  kr.kr_keys[i].kiov_len), printf("\n");
			else
				printf("%s\n", (char *)kr.kr_keys[i].kiov_base);
		}

		// Success so return
		return(0);
	}
#if 0
	// Keys requested are larger than a single GetKeyRange call
	// can return, so use IterateKeyRange. Set framesize to
	// GETKEYRANGE_MAX_COUNT to maximize network efficiency,
	// making as few server calls as possible.
	// the IterateKeyRange call throws failures when they occur
	// not catching them means more error checking of the
	// returned data so lets catch them and exit quickly.
	if (ka->ka_verbose) printf("IKR\n");
	try {
		// Init the kinetic range iterator
		unsigned int framesz = GETKEYRANGE_MAX_COUNT, i=1;

		krit = kcon->IterateKeyRange(startk, starti, endk, endi,framesz);
		while (krit != kinetic::KeyRangeEnd() && count) {
			if (ka->ka_verbose)
				printf("%u: ", i++);

			// Dump the keys as requested
			if (hdump)
				hexdump(krit->c_str(),krit->length());
			else if (adump)
				asciidump(krit->c_str(),krit->length()), printf("\n");
			else
				printf("%s\n", krit->c_str());		

			++krit; // Advance the iter

				// count == -1; means unlimited count
			if (count < 0 )
				continue;

			count--;
		}
	} catch (std::runtime_error &e) {
		printf("Iterator Failed: %s\n", e.what());
		return(-1);
	}
#endif	
	return(0);
}

