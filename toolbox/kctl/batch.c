#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

/* Cheating with globals */
static char	start, commit;

void kctl_dump(kgetlog_t *glog);

#define CMD_USAGE(_ka) kctl_batch_usage(_ka)
int
kctl_batch_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-S           Start a batch\n");
	fprintf(stderr, "\t-C           Complete/Commit a batch\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

/**
 *  Issue either the batchcreate or batchend command 
 */
int
kctl_batch(int argc, char *argv[], int kts, struct kargs *ka)
{
	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c;
	kstatus_t 	kstatus;
	
	/* clear global flag vars */
	start = commit = 0;

        while ((c = getopt(argc, argv, "SE?h")) != EOF) {
                switch (c) {
		case 'S':
			start = 1;
			if (ka->ka_batch) {
				fprintf(stderr,
					"*** Active batch already exists\n");
				CMD_USAGE(ka);
				return(-1);
			}
			break;
		case 'C':
			commit = 1;
			if (!ka->ka_batch) {
				fprintf(stderr,
					"*** No active batch to Commit\n");
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
	
	if (!start && !commit) {
                        fprintf(stderr,
				"*** Must Start or Commit\n");
			CMD_USAGE(ka);
			return(-1);
	}
	
	if (start && commit) {
                        fprintf(stderr,
				"*** Can't Start and Commit together\n");
			CMD_USAGE(ka);
			return(-1);
	}
 	
	/* Shouldn't be any other args */
	if (argc - optind) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	if (start) {
		ka->ka_batch = ki_batchcreate(kts);
		if (!ka->ka_batch) {
			printf("Batch start failed\n");
			return(-1);
		}
	} else {
		kstatus = ki_batchend(kts, ka->ka_batch);
		ka->ka_batch = NULL;
		
		if(!kstatus.ks_code) {
			printf("Batch commit failed: %s\n", kstatus.ks_message);
			return(-1);
		}
	}
	
	return(0);
}

