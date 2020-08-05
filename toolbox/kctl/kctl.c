#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"


/* Initialization must be in same struct defintion order */
struct kargs kargs = {
	/* .field       = default values, */
	.ka_progname	= (char *)"kctl",
	.ka_cmd		= KCTL_EOT,
	.ka_cmdstr	= (char *)"<none>",
	.ka_key		= (char *)"<none>",
	.ka_keylen	= 6,
	.ka_val		= (char *)"<none>",
	.ka_vallen	= 6,
	.ka_user 	= 1,
	.ka_hkey	= (char *)"asdfasdf",
	.ka_host	= (char *)"127.0.0.1",
	.ka_port	= "8123",
	.ka_usetls	= 0,
	.ka_timeout	= 10,
	.ka_clustervers = -1,
	.ka_quiet	= 0,
	.ka_terse	= 0,
	.ka_verbose	= 0,
	.ka_yes		= 0
};

extern int kctl_get(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_put(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_del(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_info(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_range(int argc, char *argv[], int kts, struct kargs *ka);

#if 0
extern int kctl_ping(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_cluster(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_lock(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_acl(int argc, char *argv[], int kts, struct kargs *ka);
#endif

int kctl_nohandler(int argc, char *argv[], int kts, struct kargs *ka);

struct ktable {
	enum kctl_cmd ktab_cmd;
	const char *ktab_cmdstr;
	const char *ktab_cmdhelp;
	int (*ktab_handler)(int, char *[], int, struct kargs *);
} ktable[] = {
#if 0
	{ KCTL_NOOP,    "ping",    "Ping the kinetic device", &kctl_ping},
#endif
	{ KCTL_GET,     "get",     "Get key value", &kctl_get},
	{ KCTL_GETNEXT, "getnext", "Get next key value", &kctl_get},
	{ KCTL_GETPREV, "getprev", "Get previous key value", &kctl_get},
	{ KCTL_GETVERS, "getvers", "Get key value version", &kctl_get},
	{ KCTL_PUT,     "put",     "Put key value", &kctl_put},
	{ KCTL_DEL,     "del",     "Delete key value(s)", &kctl_del},
	{ KCTL_GETLOG,  "info",    "Get device information", &kctl_info},
	{ KCTL_RANGE,   "range",   "Print a range of keys", &kctl_range},

#if 0
	{ KCTL_SETCLUSTERV,
	                "cluster", "Set device cluster version", &kctl_cluster},
	{ KCTL_SETLOCKPIN,
	                "setlock", "Set the lock PIN", &kctl_lock},
	{ KCTL_LOCK,    "lock",    "Lock the kinetic device", &kctl_lock},
	{ KCTL_UNLOCK,  "unlock",  "Unlock the kinetic device", &kctl_lock},
	{ KCTL_ACL,     "acl",     "Create/Modify ACL", &kctl_acl},
#endif
	
	/* End of Table (EOT) KEEP LAST */
	{ KCTL_EOT, "nocmd", "nohelp", &kctl_nohandler},
};

int	kctl(int, char *[], struct kargs *ka);

void
usage()
{
	int i;
	
        fprintf(stderr,
		"Usage: %s [COMMON OPTIONS] CMD [CMD OPTIONS] [KEY [VALUE]]\n",
		kargs.ka_progname);
	fprintf(stderr, "\nWhere, CMD is any one of these:\n");

#define US_ARG_WIDTH "-14"
	
	/* Loop through the table and print the available commands */
	for(i=0; ktable[i].ktab_cmd != KCTL_EOT; i++) {
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			ktable[i].ktab_cmdstr,
			ktable[i].ktab_cmdhelp);
	}

	/* COMMON options */
	fprintf(stderr, "\nWhere, COMMON OPTIONS are [default]:\n");
	fprintf(stderr,	"\t-h host      Hostname or IP address [%s]\n",
		kargs.ka_host);
	fprintf(stderr, "\t-p port      Port number [%s]\n", kargs.ka_port);
	fprintf(stderr, "\t-s           Use SSL [no]\n");
	fprintf(stderr, "\t-u id        User ID [%ld]\n", kargs.ka_user);
	fprintf(stderr,	"\t-m hkey      HMAC Key [%s]\n", kargs.ka_hkey);
	fprintf(stderr, "\t-c version   Client Cluster Version [0]\n");
	fprintf(stderr,	"\t-T timeout   Timeout in seconds [%d]\n",
		kargs.ka_timeout);
	fprintf(stderr, "\t-q           Be quiet [yes]\n");
	fprintf(stderr, "\t-t           Be terse [no]\n");
	fprintf(stderr, "\t-v           Be verbose [no]\n");
	fprintf(stderr, "\t-y           Automatic yes to prompts [no]\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available CMD OPTIONS: %s CMD -?\n",
		kargs.ka_progname);
        exit(2);
}

// Yes or no user input
void
print_args(struct kargs *ka)
{
#define PA_LABEL_WIDTH  "12"
	printf("%" PA_LABEL_WIDTH "s kinetic%s://%ld:%s@%s:%s/%s\n", "URL:",
	       ka->ka_usetls?"s":"", ka->ka_user, ka->ka_hkey,
	       ka->ka_host, ka->ka_port, ka->ka_cmdstr);
	
	printf("%" PA_LABEL_WIDTH "s %s\n", "Host:", ka->ka_host);
	printf("%" PA_LABEL_WIDTH "s %s\n", "Port:", ka->ka_port);
	printf("%" PA_LABEL_WIDTH "s %ld\n", "UserID:", ka->ka_user);
	printf("%" PA_LABEL_WIDTH "s %s\n", "HMAC Key:", ka->ka_hkey);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Use TLS:", ka->ka_usetls);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Timeout:", ka->ka_timeout);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Quiet:", ka->ka_quiet);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Terse:", ka->ka_terse);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Verbose:", ka->ka_verbose);
	printf("%" PA_LABEL_WIDTH "s %s\n", "Command:", ka->ka_cmdstr);
}

int
main(int argc, char *argv[])
{
	extern char     	*optarg;
        extern int		optind, opterr, optopt;
        char			c, *cp;
	int			i;

	kargs.ka_progname = argv[0];
	
        while ((c = getopt(argc, argv, "+c:h:m:p:su:tqvy?")) != EOF) {
                switch (c) {
		case 'h':
			kargs.ka_host = optarg;
			break;
		case 'm':
			kargs.ka_hkey = optarg;
			break;
		case 'p':
			kargs.ka_port = optarg;
			break;
		case 'c':
			kargs.ka_clustervers = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "*** Invalid Cluster Version %s\n",
				       optarg);
				 usage();
			}
			kargs.ka_clustervers = (int64_t) atoi(optarg);
			break;
                case 's':
                        kargs.ka_usetls = 1;
                        break;
                case 'q':
                        kargs.ka_quiet = 1;
                        break;
                case 't':
                        kargs.ka_terse = 1;
                        break;
		case 'T':
			kargs.ka_timeout = atoi(optarg);
			break;
		case 'u':
			kargs.ka_user = atoi(optarg);
			break;
                case 'v':
                        kargs.ka_verbose = 1;
                        break;
                case 'y':
                        kargs.ka_yes = 1;
                        break;
                case '?':
                        usage();
                        break;
                default:
			usage();
                        break;
                }
        }

	/* Check for the cmd [key [value]] parms */
	if (argc - optind == 0) {
		fprintf(stderr, "*** No CMD provided\n");
		usage();
	}

	/* consume cmd */
	kargs.ka_cmdstr = argv[optind++];
	
	/* Loop through the table and validate the command */
	for(i=0; i<KCTL_EOT; i++) {
		if (ktable[i].ktab_cmd == KCTL_EOT)
			break;
		
		if (strcmp(ktable[i].ktab_cmdstr, kargs.ka_cmdstr) == 0) {
			// Found a good command
			kargs.ka_cmd = ktable[i].ktab_cmd;
			break;
		}
	}

	if (i == KCTL_EOT || (ktable[i].ktab_cmd == KCTL_EOT)) {
		/* bad command */
		fprintf(stderr, "*** Bad command: %s\n", kargs.ka_cmdstr);
		usage();
	}

	if (kargs.ka_verbose && !kargs.ka_terse)
		print_args(&kargs);
	
	kctl(argc, argv, &kargs);

	exit(0);
}

int
kctl(int argc, char *argv[], struct kargs *ka)
{
	int i, rc, ktd;
	
	ktd = ki_open(ka->ka_host, ka->ka_port,
		      ka->ka_usetls, ka->ka_user, ka->ka_hkey);
	
	if (ktd < 0) {
		fprintf(stderr, "%s: Connection Failed\n", ka->ka_progname);
		return(EINVAL);
	}

	ka->ka_limits = ki_limits(ktd);
	if (!ka->ka_limits.kl_keylen) {
		fprintf(stderr, "%s: Unable to get kinetic limits\n",
			ka->ka_progname);
		return(EINVAL);
	}
		
	for(i=0; i<KCTL_EOT; i++) {
		if (ktable[i].ktab_cmd == kargs.ka_cmd) {
			/* Found a good command, call it */
			rc = (*ktable[i].ktab_handler)(argc, argv, ktd, ka);
			break;
		}
	}
	
	ki_close(ktd);
	
	return rc;
}


int
kctl_nohandler(int argc, char *argv[], int kts, struct kargs *ka)
{

	fprintf( stderr,  "Illegal call - Should never be called\n");
	return(-1);
}

