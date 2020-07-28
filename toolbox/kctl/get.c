#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define CMD_USAGE(_ka) kctl_get_usage(_ka)
int
kctl_get_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr,
		"\t-A           Dumps key/value as ascii w/escape seqs\n");
	fprintf(stderr,
		"\t-X           Dumps key/value as both hex and ascii\n");
	fprintf(stderr,
		"\t-?           Help\n");

	fprintf(stderr,
		"\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr,
		"hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr,
		R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr,
		"\nIf a conversion fails the command terminates.\n");

	fprintf(stderr,
		"\nBy default keys and values are printed as raw strings,\n");
	fprintf(stderr,
		"including special/nonprintable chars\n");

	fprintf(stderr,
		"\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
kctl_get(int argc, char *argv[], int ktd, struct kargs *ka)
{
	extern char	*optarg;
        extern int	optind, opterr, optopt;
        char		c;
	int		hdump = 0, adump = 0;
	kv_t		kv;
	struct kiovec	kv_key[1]  = {0, 0};
	struct kiovec	kv_val[1]  = {0, 0};
	kv_t		rkv;
	struct kiovec	rkv_key[1]  = {0, 0};
	struct kiovec	rkv_val[1]  = {0, 0};
	kstatus_t 	kstatus;
	
        while ((c = getopt(argc, argv, "AXh?")) != EOF) {
                switch (c) {
		case 'A':
			adump = 1;
			if (hdump) {
				fprintf(stderr,
					"*** -X and -A are exclusive\n");
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'X':
			hdump = 1;
			if (adump) {
				fprintf(stderr,
					"*** -X and -A are exclusive\n");
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

	// Check for the cmd key parm
	if (argc - optind == 1) {
		/*
		 * Aways decode any ascii arbitrary hexadecimal value escape
		 * sequences in the passed-in key, if none present this
		 * amounts to a copy.
		 */
		if (!asciidecode(argv[optind], strlen(argv[optind]),
				 (void **)&ka->ka_key, &ka->ka_keylen)) {
			fprintf(stderr, "*** Failed key conversion\n");
			CMD_USAGE(ka);
			return(-1);
		}
#if 0
		printf("%s\n", argv[optind]);
		printf("%lu\n", ka->ka_keylen);
		hexdump(ka->ka_key, ka->ka_keylen);
#endif 
	} else {
		fprintf(stderr, "*** Too few or too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	/* Init kv and return kv */
	memset(&kv, 0, sizeof(kv_t));
	kv.kv_key    = kv_key;
	kv.kv_keycnt = 1;
	kv.kv_val    = kv_val;
	kv.kv_valcnt = 1;
	
	memset(&rkv, 0, sizeof(kv_t));
	rkv.kv_key    = rkv_key;
	rkv.kv_keycnt = 1;
	rkv.kv_val    = rkv_val;
	rkv.kv_valcnt = 1;
	
	/*
	 * Hang the key
	 */
	kv.kv_key[0].kiov_base = ka->ka_key;
	kv.kv_key[0].kiov_len  = ka->ka_keylen;

	/*
	 * 4 cmd supported here: Get, GetNext, GetPrev, GetVers
	 */
	if (ka->ka_cmd == KCTL_GET)
		kstatus = ki_get(ktd, &kv);
	else if	(ka->ka_cmd == KCTL_GETNEXT)
		kstatus = ki_getnext(ktd, &kv, &rkv);
	else if	(ka->ka_cmd == KCTL_GETPREV)
		kstatus = ki_getprev(ktd, &kv, &rkv);
	else if	(ka->ka_cmd == KCTL_GETVERS)
		kstatus = ki_getversion(ktd, &kv);
	else {
		fprintf(stderr, "Bad command: %s\n", ka->ka_cmdstr);
		return(-1);
	}
		
	if(kstatus.ks_code != K_OK) {
		printf("%s failed: %s\n",
		       ka->ka_cmdstr, kstatus.ks_message);
		return(-1);
	}

	printf("Key(");
	if (adump) {
		asciidump(kv.kv_key[0].kiov_base, kv.kv_key[0].kiov_len);
	} else if (hdump) {
		printf("\n");
		hexdump(kv.kv_key[0].kiov_base, kv.kv_key[0].kiov_len);
	} else {
		printf("%s", (char *)kv.kv_key[0].kiov_base);
	}
	printf("): ");
	
	if (ka->ka_cmd == KCTL_GETVERS) {
		printf("%s\n", (char *)kv.kv_vers);
		return(0);
	}

	printf("\nLength: %lu\n", kv.kv_val[0].kiov_len);
	if (adump) {
		asciidump(kv.kv_val[0].kiov_base, kv.kv_val[0].kiov_len);
	} else if (hdump) {
		hexdump(kv.kv_val[0].kiov_base, kv.kv_val[0].kiov_len);
	} else {
		printf("%s", (char *)kv.kv_val[0].kiov_base);
	}
	printf("\n");
	return(0);
}

