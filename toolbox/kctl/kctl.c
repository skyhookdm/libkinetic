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
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

char stdport[] = "8123";
char tlsport[] = "8443";

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
	.ka_pass	= (char *)"asdfasdf",
	.ka_host	= (char *)"127.0.0.1",
	.ka_port	= stdport,
	.ka_usetls	= 0,
	.ka_clustervers = -1,
	.ka_batch	= NULL,
	.ka_timeout	= 10,
	.ka_quiet	= 0,
	.ka_terse	= 0,
	.ka_verbose	= 0,
	.ka_input	= KCTL_CMDLINE,
	.ka_yes		= 0
};

static char kctl_vers[40];
static uint32_t kctl_vers_num =
	(KCTL_VERS_MAJOR*1E6) + (KCTL_VERS_MINOR*1E3) + KCTL_VERS_PATCH;

extern int kctl_get(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_put(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_del(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_info(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_range(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_batch(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_stats(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_ping(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_flush(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_exec(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_device(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_pin(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_acl(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_upgrade(int argc, char *argv[], int kts, struct kargs *ka);
extern int kctl_cluster(int argc, char *argv[], int kts, struct kargs *ka);

int kctl_nohandler(int argc, char *argv[], int kts, struct kargs *ka);

struct ktable {
	enum kctl_cmd ktab_cmd;
	const char *ktab_cmdstr;
	const char *ktab_cmdhelp;
	int (*ktab_handler)(int, char *[], int, struct kargs *);
} ktable[] = {
	{ KCTL_NOOP,    "ping",    "Ping the Kinetic device", &kctl_ping},
	{ KCTL_GET,     "get",     "Get key value", &kctl_get},
	{ KCTL_GETNEXT, "getnext", "Get next key value", &kctl_get},
	{ KCTL_GETPREV, "getprev", "Get previous key value", &kctl_get},
	{ KCTL_GETVERS, "getvers", "Get key value version", &kctl_get},
	{ KCTL_PUT,     "put",     "Put key value", &kctl_put},
	{ KCTL_DEL,     "del",     "Delete key value(s)", &kctl_del},
	{ KCTL_GETLOG,  "info",    "Get Kinetic device information", &kctl_info},
	{ KCTL_RANGE,   "range",   "Print a range of keys", &kctl_range},
	{ KCTL_BATCH,   "batch",   "Start or End a batch", &kctl_batch},
	{ KCTL_STATS,   "stats",   "Enable command statistics", &kctl_stats},
	{ KCTL_FLUSH,   "flush",   "Flush key values caches", &kctl_flush},
	{ KCTL_EXEC,    "exec",    "Execute a func on the Kinetic device", &kctl_exec},
	{ KCTL_DEVICE,  "device",  "[Un]Lock, erase the Kinetic device", &kctl_device},
	{ KCTL_PIN,     "pin",     "Set the erase or lock PINs", &kctl_pin},
	{ KCTL_ACL,     "acl",     "Set the Kinetic ACLs", &kctl_acl},
	{ KCTL_UPGRADE, "upgrade", "Upgrade the Kinetic device firmware", &kctl_upgrade},
	{ KCTL_CLUSTER, "cluster", "Get/Set Kinetic cluster version", &kctl_cluster},

	/* End of Table (EOT) KEEP LAST */
	{ KCTL_EOT, "nocmd", "nohelp", &kctl_nohandler},
};

int	kctl(int, char *[], struct kargs *ka);
int	kctl_interactive(struct kargs *ka);

void
usage()
{
	int i;

	if (kargs.ka_input == KCTL_CMDLINE) {
		fprintf(stderr,	"Usage: %s [COMMON OPTIONS] ",
			kargs.ka_progname);
	}

	fprintf(stderr,	"[CMD [CMD OPTIONS]]\n");
	fprintf(stderr, "\nWhere, CMD is any one of these:\n");


#define US_ARG_WIDTH "-14"

	/* Loop through the table and print the available commands */
	for(i=0; ktable[i].ktab_cmd != KCTL_EOT; i++) {
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			ktable[i].ktab_cmdstr,
			ktable[i].ktab_cmdhelp);
	}

	if (kargs.ka_input !=  KCTL_CMDLINE) {
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"help", "Show Help");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"echo <text>", "Print some text");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"env", "Show kctl environment");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"verbose", "Toggle verbose mode");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"version", "Show kctl version information");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"yes", "Toggle yes mode");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"quit", "Exit kctl");

		fprintf(stderr,"\n\t%" US_ARG_WIDTH "s%s\n",
			"CMD -h", "Shows help for CMD");
		return;
	}

	fprintf(stderr, "\nIf no CMD is given, interactive mode is invoked.\n");

	/* COMMON options */
	fprintf(stderr, "\nWhere, COMMON OPTIONS are [default]:\n");
	fprintf(stderr,	"\t-h host      Hostname or IP address [%s]\n",
		kargs.ka_host);
	fprintf(stderr, "\t-p port      Port number [%s]\n", kargs.ka_port);
	fprintf(stderr, "\t-s           Use SSL [no]\n");
	fprintf(stderr, "\t-u id        User ID [%ld]\n", kargs.ka_user);
	fprintf(stderr,	"\t-P pass      User Password [%s]\n", kargs.ka_pass);
	fprintf(stderr, "\t-c version   Client Cluster Version [0]\n");
	fprintf(stderr,	"\t-T timeout   Timeout in seconds [%d]\n",
		kargs.ka_timeout);
	fprintf(stderr,	"\t-S           Enable kctl stats\n");
	fprintf(stderr, "\t-q           Be quiet [yes]\n");
	fprintf(stderr, "\t-t           Be terse [no]\n");
	fprintf(stderr, "\t-v           Be verbose [no]\n");
	fprintf(stderr, "\t-y           Automatic yes to prompts [no]\n");
	fprintf(stderr, "\t-f filename  Replace stdin with filename;\n");
	fprintf(stderr, "\t                 forces interactive mode\n");
	fprintf(stderr, "\t-V           Show versions\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available CMD OPTIONS: %s CMD -?\n",
		kargs.ka_progname);
        exit(2);
}

// Yes or no user input
void
print_args(struct kargs *ka)
{
#define PA_LABEL_WIDTH  "16"
	printf("%" PA_LABEL_WIDTH "s kinetic%s://%ld:%s@%s:%s/%s\n", "URL:",
	       ka->ka_usetls?"s":"", ka->ka_user, ka->ka_pass,
	       ka->ka_host, ka->ka_port, ka->ka_cmdstr);

	printf("%" PA_LABEL_WIDTH "s %s\n", "Host:", ka->ka_host);
	printf("%" PA_LABEL_WIDTH "s %s\n", "Port:", ka->ka_port);
	printf("%" PA_LABEL_WIDTH "s %ld\n","User ID:", ka->ka_user);
	printf("%" PA_LABEL_WIDTH "s %s\n", "User Password:", ka->ka_pass);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Use TLS:", ka->ka_usetls);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Timeout:", ka->ka_timeout);
	printf("%" PA_LABEL_WIDTH "s %ld\n", "Cluster Version:",
	       ka->ka_clustervers);
	printf("%" PA_LABEL_WIDTH "s %p\n", "Batch:", ka->ka_batch);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Yes:", ka->ka_yes);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Quiet:", ka->ka_quiet);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Terse:", ka->ka_terse);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Verbose:", ka->ka_verbose);
	printf("%" PA_LABEL_WIDTH "s %s\n", "Command:", ka->ka_cmdstr);
}

void
print_version()
{
	kversion_t *kver;

	kver = ki_create(-1, KVERSION_T);
	ki_version(kver);

	// `if` is just to get rid of compiler warnings
	if (kctl_vers_num)  {
		sprintf(kctl_vers
			,"%d.%d.%d"
			,KCTL_VERS_MAJOR, KCTL_VERS_MINOR, KCTL_VERS_PATCH
		);
	}

	printf("KCTL Version: %s\n"            , kctl_vers);
	printf("Kinetic Protobuf Version: %s\n", kver->kvn_pb_kinetic_vers);
	printf("Kinetic Library Version: %s\n" , kver->kvn_ki_vers);
	printf("Kinetic Library Git Hash: %s\n", kver->kvn_ki_githash);
	printf("Protobuf C Version: %s\n"      , kver->kvn_pb_c_vers);

	ki_destroy(kver);
}


/**
 * Permit several KCTL environment variables to override the defaults
 */
void
kctl_getenv(struct kargs *ka)
{
	char *val, *lval, *cp;
	int i, port=0;
	uint32_t tls;
	int64_t user;

	if ((val = getenv("KCTL_USER"))) {
		user = strtoll(val, &cp, 0);
		if (!cp || *cp != '\0') {
			fprintf(stderr,
				"*** Warning: Invalid USER in environment\n");
		} else {
			/* Only set it if it was a good numeric conversion */
			ka->ka_user = user;
		}
	}
		
	if ((val = getenv("KCTL_PASS"))) {
		ka->ka_pass = val;
	}
		
	if ((val = getenv("KCTL_HOST"))) {
		ka->ka_host = val;
	}
		
	if ((val = getenv("KCTL_PORT"))) {
		ka->ka_port = val;
		port = 1;
	}

	/* Order is import, usetls check must follow port check */
	if ((val = getenv("KCTL_USETLS"))) {
		/* Support either 0/nonzero or true/false/yes/no */
		tls = (strtol(val, &cp, 0)?1:0);
		if (!cp || *cp != '\0') {
			/* Failed numeric check, look for true/false/yes/no */
			lval = strdup(val);

			/* Canonicalize */
			for (i=0; i<strlen(val); i++)
				lval[i] = tolower(val[i]);
			
			if (!strcmp(lval, "true") || !strcmp(lval, "yes"))
				ka->ka_usetls = 1;
			else if (!strcmp(lval, "false") || !strcmp(lval, "no"))
				ka->ka_usetls = 0;
			else
				fprintf(stderr,
					"*** Warning: Invalid USETLS in environment\n");
			free(lval);
		} else {
			/* Only set it if it was a good numeric conversion */
			ka->ka_usetls = tls;
		}

		/*
		 * As a convenience, adjust the default port
		 * to the TLS port is none has been provided.
		 */
		if (ka->ka_usetls && !port) 
			ka->ka_port = tlsport;
	}
}


int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int   optind, opterr, optopt;
	FILE *f;
	char         c, *cp;
	int          i, pflag=0;

	kargs.ka_progname = argv[0];

	kctl_getenv(&kargs);
		
	while ((c = getopt(argc, argv, "+c:f:h:P:p:qsSu:tvVy?")) != (char)EOF) {
		switch (c) {
		case 'h':
			kargs.ka_host = optarg;
			break;

		case 'P':
			kargs.ka_pass = optarg;
			break;

		case 'p':
			pflag=1;
			kargs.ka_port = optarg;
			break;

		case 'c':
			kargs.ka_clustervers = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr,
					"*** Invalid Cluster Version %s\n",
					optarg);
				usage();
			}
			break;

		case 'f':
			/* Reset stdin for interactive mode */
			if ((f = freopen(optarg, "r", stdin )) == NULL) {
				perror("open");
				usage();
			}
			kargs.ka_input = KCTL_SCRIPT;
			break;

		case 's':
			if (!pflag) {
				/*
				 * As a convenience, adjust the default port
				 * to the TLS port is none has been provided.
				 */
				kargs.ka_port = tlsport;
			}
			kargs.ka_usetls = 1;
			break;

		case 'S':
			kargs.ka_stats = 1;
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

		case 'V':
			print_version();
			exit(0);

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

	/* Check for the cmd [key [value]] parms set input as appropriate */
	if ((argc - optind == 0) || (kargs.ka_input != KCTL_CMDLINE)) {
		if (kargs.ka_input ==  KCTL_CMDLINE)
			kargs.ka_input =  KCTL_INTERACTIVE;

		if (kargs.ka_verbose && (kargs.ka_input == KCTL_INTERACTIVE))
			printf("Going interactive...\n");

		kctl_interactive(&kargs);
		exit(0);
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
kctl_start(struct kargs *ka)
{
	int ktd;

	ktd = ki_open(ka->ka_host, ka->ka_port,
		      ka->ka_usetls, ka->ka_user, ka->ka_pass);

	if (ktd < 0) {
		fprintf(stderr, "%s: Connection Failed\n", ka->ka_progname);
		return(-1);
	}

	ka->ka_limits = ki_limits(ktd);
	if (!ka->ka_limits.kl_keylen) {
		fprintf(stderr, "%s: Unable to get kinetic limits\n",
			ka->ka_progname);
		return(-1);
	}

	return(ktd);
}


int
kctl(int argc, char *argv[], struct kargs *ka)
{
	int i, rc, ktd;

	ktd = kctl_start(ka);
	if (ktd < 0) {
		fprintf(stderr, "%s: Unable to start\n", ka->ka_progname);
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
kctl_interactive(struct kargs *ka)
{
	enum { maxargs = 1024 };
	char *line, *sline, *p, *argv[maxargs];
	int i, j, rc, ktd, argc, nargc, unmatchedq;
	size_t len;
	ssize_t br;
	extern char     	*optarg;
        extern int		optind, opterr, optopt;

	ktd = kctl_start(ka);
	if (ktd < 0) {
		fprintf(stderr, "%s: Unable to start\n", ka->ka_progname);
		return(EINVAL);
	}

	while (1) {
		/* Either interactive mode or script mode */
		if (ka->ka_input == KCTL_INTERACTIVE) {
			if ((line = readline("kctl> ")) == 0) {
				break;
			}
			/* dup the line for use in the history */
			add_history(line);
		} else {
			/* KCTL_SCRIPT */
			line = NULL;
			if ((br = getline(&line, &len, stdin))  < 0) {
				break;
			}
			line[br-1] ='\0'; /* drop the newline */
		}

		/* save a copy of the ptr as tokenizing will lose it */
		sline = line;

		/* Create the argv array, we will lose spaces in quoted str */
		argc = 0;
		p = strtok(line, " \t");
		while (p && argc < maxargs-1) {
			argv[argc++] = p;
			p = strtok(0, " \t");
		}
		argv[argc] = NULL;

		/*
		 * Tokeninizing turned quoted strings in multiple argv
		 * elements.  Go through and reassemble the strings
		 * into a single argv element without quotes.  This is a
		 * *VERY* simplistic approach. the tokenizer above has
		 * already stripped out redundant spaces from the string.
		 * This approach also doesn't handle quotes in the middle
		 * of a word and it only handles double quotes and not
		 * single quotes.
		 * Comments are permitted both for the entire as well as
		 * partial lines. All elements after a comment char (#)
		 * are ignored. Comment chars must follow whitespace. If
		 * in a string they will part of the string. If in the
		 * middle of token they will be part of that token.
		 * The argv is copied so that it can be freed as a unit
		 * at the bottom, regardless if merges did or did not occur
		 * on any given element.
		 */
		unmatchedq=0;
		/* k tracks base argv placement*/
		for(i=0, nargc=0; i<argc; nargc++, i++) {
			char *s, *ns;
			int n;

			/* Make a copy to permit easier freeing */
			s = strdup(argv[i]);
			argv[nargc] = s;

			/* If a comment is found ignore remain arguments */
			if (s[0] == '#') { break; }

			/* No starting quote continue looking */
			if (s[0] != '"') { continue; }

			/* Found a starting quote, drop the quote */
			ns = malloc(strlen(s));
			strcpy(ns, &s[1]);
			free(s);
			s = ns;
			argv[nargc] = s;

			/* Only one quote so far */
			unmatchedq=1;

			/* Look for arg with the trailing quote */
			for (j=i; j<argc; j++) {

				/* Is the last char the quote? */
				n = strlen(argv[j]);
				if (argv[j][n-1] == '"') {
					unmatchedq=0;
					argv[j][n-1] = '\0';/* drop the quote */
				}

				if (j!=i) {
					/* Combine args, space and null */
					ns = malloc(strlen(s) + n + 1 + 1);
					strcpy(ns, s);
					strcat(ns, " ");
					strcat(ns, argv[j]);
					free(s);  /* free the original */
					s = ns;
					argv[nargc] = s;
				}
				if (!unmatchedq) { break; }
			}

			/* if we merged anything need to bump up i */
			i = j;
		}

		argv[nargc] = NULL;
		argc = nargc;

		if (!argc) goto next;

		if (ka->ka_input == KCTL_SCRIPT) {
			printf("kctl> %s", argv[0]);
			for(i=1; i<argc; i++) {
				printf(" %s", argv[i]);
			}
			printf("\n");
		}
		
		if (argv[0][0] != '#')

		if (unmatchedq) {
			fprintf(stderr, "Unmatched quotes\n");
			goto next;
		}

		if (ka->ka_verbose)
			for(i=0; i<argc; i++)
				printf("ARGV[%d] -> %s\n", i, argv[i]);

		if (strcmp(argv[0], "quit") == 0)
			break;

		if (strcmp(argv[0], "echo") == 0) {
			if (argc > 1) {
				printf("%s", argv[1]);
				for(i=2; i<argc; i++) {
					printf(" %s", argv[i]);
				}
			}
			printf("\n");
			goto next;
		}

		if (strcmp(argv[0], "help") == 0) {
			usage();
		        goto next;
		}

		if (strcmp(argv[0], "env") == 0) {
			print_args(ka);
			goto next;
		}

		if (strcmp(argv[0], "verbose") == 0) {
			if (ka->ka_verbose) {
				ka->ka_verbose = 0;
				printf("Verbose Mode in OFF\n");
			} else {
				ka->ka_verbose = 1;
				printf("Verbose Mode in ON\n");
			}
			goto next;
		}

		if (strcmp(argv[0], "version") == 0) {
			print_version();
		        goto next;
		}

		if (strcmp(argv[0], "yes") == 0) {
			if (ka->ka_yes) {
				ka->ka_yes = 0;
				printf("Yes Mode in OFF\n");
			} else {
				ka->ka_yes = 1;
				printf("Yes Mode in ON\n");
			}
			goto next;
		}

		optind = 0;
		ka->ka_cmdstr = argv[0];
		for(i=0; i<KCTL_EOT; i++) {
			if (ktable[i].ktab_cmd == KCTL_EOT)
				break;

			if (strcmp(ktable[i].ktab_cmdstr, ka->ka_cmdstr) == 0) {
				// Found a good command
				kargs.ka_cmd = ktable[i].ktab_cmd;

				rc = (*ktable[i].ktab_handler)(argc, argv,
							       ktd, ka);
				break;
			}
		}

		if ((i == KCTL_EOT) ||(ktable[i].ktab_cmd == KCTL_EOT))
			fprintf(stderr, "%s: Command not found\n", argv[0]);

	next:
		/* free the saved line ptr and go around again */
		for(i=0; i<argc; i++) {
			free(argv[i]);
		}
		free(sline);
	}

	if (ka->ka_verbose)
		printf("\nkctl exiting\n");

	ki_close(ktd);

	return rc;
}

int
kctl_nohandler(int argc, char *argv[], int kts, struct kargs *ka)
{

	fprintf( stderr,  "Illegal call - Should never be called\n");
	return(-1);
}

