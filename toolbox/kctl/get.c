#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
	fprintf(
		stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ka->ka_progname,
		ka->ka_cmdstr
	);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-A           Dumps key/value as ascii w/escape seqs\n");
	fprintf(stderr, "\t-X           Dumps key/value as both hex and ascii\n");
	fprintf(stderr, "\t-?           Help\n");

	fprintf(stderr, "\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");

	fprintf(stderr, "\nBy default keys and values are printed as raw strings,\n");
	fprintf(stderr, "including special/nonprintable chars\n");

	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

int
kctl_get(int argc, char *argv[], int ktd, struct kargs *ka)
{
	extern char   *optarg;
	extern int     optind, opterr, optopt;
	char           c, *rkey;
	int            hdump = 0, adump = 0, kctl_status = 0;

	kv_t           kv;
	struct kiovec  kv_key[1]  = {0, 0};
	struct kiovec  kv_val[1]  = {0, 0};

	kv_t           rkv;
	struct kiovec  rkv_key[1] = {0, 0};
	struct kiovec  rkv_val[1] = {0, 0};

	kstatus_t      kstatus;
	kv_t          *pkv;

	// This while loop can return early because there are no allocs to manage
	while ((c = getopt(argc, argv, "AXh?")) != EOF) {
		switch (c) {
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
	 * NOTE: all of these calls make allocations which need to be cleaned before exit
	 */
	switch (ka->ka_cmd) {
	case KCTL_GET:
		kstatus = ki_get(ktd, &kv);
		pkv     = &kv;
		break;

	case KCTL_GETNEXT:
		kstatus = ki_getnext(ktd, &kv, &rkv);
		pkv     = &rkv;
		break;

	case KCTL_GETPREV:
		kstatus = ki_getprev(ktd, &kv, &rkv);
		pkv     = &rkv;
		break;

	case KCTL_GETVERS:
		kstatus = ki_getversion(ktd, &kv);
		pkv     = &kv;
		break;

	default:
		// no allocation, so we can still return early
		fprintf(stderr, "Bad command: %s\n", ka->ka_cmdstr);
		return (-1);
	}

	switch (kstatus.ks_code) {
	case K_OK:
		break;

	case K_ENOTFOUND:
		printf("%s: No key found.\n", ka->ka_cmdstr);

		kctl_status = (-1);
		goto kctl_gex;

	default:
		printf("%s: failed: status code %d %s\n",
			   ka->ka_cmdstr, kstatus.ks_code,
			   kstatus.ks_message);

		kctl_status = (-1);
		goto kctl_gex;
	}

	// ------------------------------
	// Print key name
	printf("Key(");
	if (adump) {
		asciidump(pkv->kv_key[0].kiov_base, pkv->kv_key[0].kiov_len);
	}

	else if (hdump) {
		printf("\n");
		hexdump(pkv->kv_key[0].kiov_base, pkv->kv_key[0].kiov_len);
	}

	else {
		/* add null byte to print as a string */
		rkey = strndup(
			(char *) pkv->kv_key[0].kiov_base,
			         pkv->kv_key[0].kiov_len
		);

		printf("%s", rkey);
		free(rkey);
	}
	printf("): ");

	// ------------------------------
	// Print key version
	if (ka->ka_cmd == KCTL_GETVERS) {
		printf("%s\n", (pkv->kv_ver ? (char *) pkv->kv_ver : "N/A"));

		kctl_status = (-1);
		goto kctl_gex;
	}

	// ------------------------------
	// Print key length
	printf("\nLength: %lu\n", pkv->kv_val[0].kiov_len);
	if (adump) {
		asciidump(pkv->kv_val[0].kiov_base, pkv->kv_val[0].kiov_len);
	}

	else if (hdump) {
		hexdump(pkv->kv_val[0].kiov_base, pkv->kv_val[0].kiov_len);
	}

	else {
		char *val_with_null = strndup(
			 (char *) pkv->kv_val[0].kiov_base
			,         pkv->kv_val[0].kiov_len
		);

		printf("%s", val_with_null);
		free(val_with_null);
	}

	printf("\n");

 kctl_gex:

	free_cmdstatus(&kstatus);
	pkv->destroy_protobuf(pkv);

	return kctl_status;
}

