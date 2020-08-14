#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

#define HAVE_RANGE ((count>-1)||start||starti||end||endi||all)
#define VERLEN 11

#define CMD_USAGE(_ka) kctl_del_usage(_ka)
int
kctl_del_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS] KEY\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-b           Add to current batch [no]\n");
	fprintf(stderr, "\t-c           Compare and delete [no]\n");
	fprintf(stderr, "\t-p [wt|wb|f] Persist Mode: writethrough, writeback, \n");
	fprintf(stderr, "\t             flush [writeback]\n");
	fprintf(stderr, "\t-a           Delete all keys\n");
	fprintf(stderr, "\t-n count	Number of keys in range[all in range]\n");
	fprintf(stderr, "\t-s KEY       Start Key in the range, non inclusive\n");
	fprintf(stderr, "\t-S KEY       Start Key in the range, inclusive\n");
	fprintf(stderr, "\t-e KEY       End Key in the range, non inclusive\n");
	fprintf(stderr, "\t-E KEY       End Key in the range, inclusive\n");
	fprintf(stderr, "\t-A           Show keys as modified ascii string\n");
	fprintf(stderr, "\t-X           Show keys as hex and ascii\n");
	fprintf(stderr, "\t-?           Help\n");

	// R"foo(....)foo" is a non escape char evaluated string literal 
	fprintf(stderr, "\nWhere, KEY is a quoted string that can contain arbitrary\n");
	fprintf(stderr, "hexidecimal escape sequences to encode binary characters.\n");
	fprintf(stderr, R"foo(Only \xHH escape sequences are converted, ex \xF8.)foo");
	fprintf(stderr, "\nIf a conversion fails the command terminates.\n");
	
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
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
        extern int	optind, opterr, optopt;
        char		c, *cp;
	kcachepolicy_t	cpolicy = KC_WB;
	char 		start = 0, starti = 0;
	char		end = 0, endi = 0;
	char		all = 0;
	char  		*startk = "";  		// Empty start in case none 
	char 		*endk = "";
        int 		count = -1;
	int 		cmpdel = 0, bat=0;
	char		newver[VERLEN]; 	// holds hex representation of
						// one int: "0x00000000"
	kv_t		kv;
	struct kiovec	kv_key[1]  = {0, 0};
	struct kiovec	kv_val[1]  = {0, 0};
	kstatus_t 	kstatus;

        while ((c = getopt(argc, argv, "abcp:s:S:e:E:n:h?")) != EOF) {
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
		case 'p':
			if (strlen(optarg) > 2) {
				fprintf(stderr, "**** Bad -p flag option %s\n",
					optarg);
				kctl_del_usage(ka);
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
		case 'n':
			if (all) {
				fprintf(stderr,
					"**** can't have a count with -a\n");
				CMD_USAGE(ka);
				return(-1);
			}
			count = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "**** Invalid count %s\n",
				       optarg);
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'a':
			if ((count > -1)||starti||start||endi||end) {
				fprintf(stderr,
					"**** -a can't be used with -[nsSeE]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			all = 1;
			starti = 1;  // all is obviously inclusive
			endi = 1;
			break;
		case 's':
			if (starti||all) {
				fprintf(stderr, "**** only one of -[asS]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			start = 1;
			startk = optarg;
			break;
		case 'S':
			if (start||all) {
				fprintf(stderr, "**** only one of -[asS]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			starti = 1;
			startk = optarg;
			break;
		case 'e':
			if (end||all) {
				fprintf(stderr, "**** only one of -[aeE]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			endk = optarg;
		        end = 1;
			break;
		case 'E':
			if (endi||all) {
				fprintf(stderr, "**** only one of -[aeE]\n");
				CMD_USAGE(ka);
				return(-1);
			}
			endk = optarg;
			endi = 1;
			break;
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	/* Init kv */
	memset(&kv, 0, sizeof(kv_t));
	kv.kv_key    = kv_key;
	kv.kv_keycnt = 1;
	kv.kv_val    = kv_val;
	kv.kv_valcnt = 1;

	/*
	 *  Check for the key parm, 
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
	
		/*
		 * Check and hang the key
		 */
		if (ka->ka_keylen > ka->ka_limits.kl_keylen ||
		    ka->ka_vallen > ka->ka_limits.kl_vallen) {
			fprintf(stderr, "*** Key and/or value too long\n");
			return(-1);
		}
 
		kv.kv_key[0].kiov_base = ka->ka_key;
		kv.kv_key[0].kiov_len  = ka->ka_keylen;

		/* 
		 * Get the key to prove the it exists 
		 * and to get the current version
		 */
		kstatus = ki_getversion(ktd, &kv);
		if (kstatus.ks_code != K_OK) {
			fprintf(stderr, "%s: %s\n", 
				ka->ka_cmdstr,  kstatus.ks_message);
			return(-1);
		}

		kv.kv_cpolicy  = cpolicy;

		if (ka->ka_verbose) {
			printf("Compare & Delete: %s\n",
			       cmpdel?"Enabled":"Disabled");
			printf("Current Version:  %s\n", (char *)kv.kv_ver);
		}
	
		if (ka->ka_yes && !ka->ka_quiet) 
			printf("***DELETING Key: ");
		else
			printf("***DELETE? Key: ");
		asciidump(ka->ka_key, ka->ka_keylen);
		printf("\n");

		/* ka_yes must be first to short circuit the conditional */
		if (ka->ka_yes || yorn("Please answer y or n [yN]: ", 0, 5)) {
			if (cmpdel)
				kstatus = ki_cad(ktd,
						 (bat?ka->ka_batch:NULL), &kv);
			else
				kstatus = ki_del(ktd,
						 (bat?ka->ka_batch:NULL), &kv);
				
			if (kstatus.ks_code != K_OK) {
				fprintf(stderr,
					"%s: Unable to delete key: %s: %s\n", 
					ka->ka_cmdstr,  kstatus.ks_message,
					kstatus.ks_detail?kstatus.ks_detail:"");
				return(-1);
			}
		}
	
		return(0);

	} else if ((argc - optind == 0) && (HAVE_RANGE)) {
#if 0
		/* PAK; Add ki_iter support here, look at range.c */
		/* 
		 * No key but a range define, bueno.
		 *
		 * This is the range key delete, could be all, a key range, or
		 * a count limited key range.
		 * 
		 * Setup the range
		 * if all is set then start and end are unset, which is good as
		 * all = (start="" and end="all FFs"), inclusive
		 * 
		 * If no start key provided, nothing to do as the empty string
		 * would act as the first possible key
		 * 
		 * if no end key provided use the last possible key which is a
		 * string of all FFs. The size of that string should be equal
		 * to the max key length.
		 * 
		 * Get max key size from GetLog(LIMITS).
		 */
		if (!endk.size()) {
			types.push_back(Command_GetLog_Type_LIMITS);
			kstatus = kcon->GetLog(types, log);
			if(!kstatus.ok()) {
				printf("Get limits failed: %s\n",
				       kstatus.message().c_str());
				return(-1);
			}

			/* 
			 * No end given, use all FFs - is the last possible key
			 */
			for (int i = 0; i < log->limits.max_key_size; i++) {
				endk += "\xFF";
			}
		}

		/* go ahead and ask the question or if ka_yes tell */
		printf("%s ", (ka->ka_yes)?"***DELETING":"***DELETE");
		
		if (all) {
			printf("ALL Keys");
		} else {
			printf("Key Range [");
			if (!start && !starti)
				printf("{START}");
			else
				asciidump(startk.data(),
					  (startk.length()<5)?startk.length():5);
			/* mark as inclusive if required */
			printf(" %s:",starti?"<i>":""); 
			
			if (!end && !endi)
				printf("{END}");
			else
				asciidump(endk.data(),
					  (endk.length()<5)?endk.length():5);

			/* mark as inclusive if required */
			printf(" %s:",endi?"<i>":"");

			if (count > 0)
				printf("%u]", count);
			else
				printf("unlimited]");

		}
			
		/* ka_yes must be first to short circuit the conditional */
		if (!ka->ka_yes &&
		    !(yorn("?\nPlease answer y or n [yN]: ", 0, 5))) {
			return(0);
		} else printf("\n");

		// Green Light - deleting from here
		// printf("DELETED\n");
		kinetic::KeyRangeIterator krit = KeyRangeIterator();
		try {
			// Init the kinetic range iterator
			size_t maxrange = ka->ka_limits.kl_rangekeycnt;
			int i=1;

			krit = kcon->IterateKeyRange(startk, starti,
						     endk, endi,framesz);
			while (krit != kinetic::KeyRangeEnd() && count) {
				if (ka->ka_verbose)
					printf("%u: ", i++);

				// If cmpdel get the current version,
				// we already know the key exists
				// If we fail to get the version keep
				// going, modeling
				// after rm(1) behaviour.
				KineticStatus kstatus = kcon->GetVersion(*krit,
									 version);
				if(!kstatus.ok()) {
					fprintf(stderr,
						"%s: Unable to find key: %s: ",
						ka->ka_cmdstr,
						kstatus.message().c_str());
					asciidump(krit->data(), krit->length());
					printf("\n");
					goto next;
				}
				
				// wmode sets whether this is a std del or
				// a cmp then del
				// If we fail to delete a key keep going, modeling
				// after rm(1) behaviour.
				kstatus = kcon->Delete(*krit, version->c_str(),
						       wmode, pmode);
				if(!kstatus.ok()) {
					fprintf(stderr,
						"%s: Unable to delete key: %s: ",
						ka->ka_cmdstr,
						kstatus.message().c_str());
					asciidump(krit->data(), krit->length());
					printf("\n");
				}

			next:
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
		return(0);
#endif /* 0 */
		printf("Ranges unsupported for now\n");
		return(-1);
	} else if ((argc - optind == 1) && (HAVE_RANGE)) {
		/* A Key and a Range, no bueno. */
		fprintf(stderr,
			"**** Key provided with range arguments defined\n");
		CMD_USAGE(ka);
		return(-1);
	} else  {
		/* No Key and No range, no bueno */
		fprintf(stderr, "**** No key and no range provided\n");
		CMD_USAGE(ka);
		return(-1);
	} 

	return(0);
}

