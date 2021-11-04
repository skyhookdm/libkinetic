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

#include <kinetic/basickv.h>
#include "bkv.h"

/* Initialization must be in same struct defintion order */
struct bargs bargs = {
	/* .field       = default values, */
	.ba_progname	= (char *)"bkv",
	.ba_cmd		= BKV_EOT,
	.ba_cmdstr	= (char *)"<none>",
	.ba_key		= (char *)"<none>",
	.ba_keylen	= 6,
	.ba_val		= (char *)"<none>",
	.ba_vallen	= 6,
	.ba_cinfo 	= {"127.0.0.1", "8123", 1, "asdfasdf", 0},
	.ba_quiet	= 0,
	.ba_terse	= 0,
	.ba_verbose	= 0,
	.ba_input	= BKV_CMDLINE,
	.ba_yes		= 0,
	.ba_limits	= {0, 0, 0}, 
};

static char bkv_vers[40];
static uint32_t bkv_vers_num =
	(BKV_VERS_MAJOR*1E6) + (BKV_VERS_MINOR*1E3) + BKV_VERS_PATCH;

extern int b_get(int argc, char *argv[], int kts, struct bargs *ba);
extern int b_put(int argc, char *argv[], int kts, struct bargs *ba);
extern int b_del(int argc, char *argv[], int kts, struct bargs *ba);
extern int b_exists(int argc, char *argv[], int kts, struct bargs *ba);
extern int b_limits(int argc, char *argv[], int kts, struct bargs *ba);
int kctl_nohandler(int argc, char *argv[], int kts, struct bargs *ba);

struct btable {
	enum bkv_cmd btab_cmd;
	const char *btab_cmdstr;
	const char *btab_cmdhelp;
	int (*btab_handler)(int, char *[], int, struct bargs *);
} btable[] = {
	{ BKV_GET,	"get",		"Get value from a key",	&b_get},
	{ BKV_GETN,	"getn",		"Get value as N keys",	&b_get},
	{ BKV_PUT,	"put",		"Put value with a key",	&b_put},
	{ BKV_PUTN,	"putn",		"Put value as N keys",	&b_put},
	{ BKV_DEL,	"del",		"Delete key value",	&b_del},
	{ BKV_EXISTS,	"exists",	"Does key exist",	&b_exists},
	{ BKV_LIMITS,	"limits",	"Display BKV limits",	&b_limits},

	/* End of Table (EOT) KEEP LAST */
	{ BKV_EOT, "nocmd", "nohelp", &kctl_nohandler},
};

int	bkv(int, char *[], struct bargs *ba);
int	b_interactive(struct bargs *ba);

void
usage()
{
	int i;

	if (bargs.ba_input == BKV_CMDLINE) {
		fprintf(stderr,	"Usage: %s [COMMON OPTIONS] ",
			bargs.ba_progname);
	}

	fprintf(stderr,	"CMD [CMD OPTIONS] [KEY [VALUE]]\n");
	fprintf(stderr, "\nWhere, CMD is any one of these:\n");


#define US_ARG_WIDTH "-14"

	/* Loop through the table and print the available commands */
	for(i=0; btable[i].btab_cmd != BKV_EOT; i++) {
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			btable[i].btab_cmdstr,
			btable[i].btab_cmdhelp);
	}

	if (bargs.ba_input !=  BKV_CMDLINE) {
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"help", "Show Help");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"echo <text>", "Print some text");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"env", "Show bkv environment");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"verbose", "Toggle verbose mode");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"version", "Show bkv version information");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"yes", "Toggle yes mode");
		fprintf(stderr,"\t%" US_ARG_WIDTH "s%s\n",
			"quit", "Exit bkv");

		fprintf(stderr,"\n\t%" US_ARG_WIDTH "s%s\n",
			"CMD -h", "Shows help for CMD");
		return;
	}

	/* COMMON options */
	fprintf(stderr, "\nWhere, COMMON OPTIONS are [default]:\n");
	fprintf(stderr,	"\t-h host      Hostname or IP address [%s]\n",
		bargs.ba_cinfo.bkvo_host);
	fprintf(stderr, "\t-p port      Port name or number [%s]\n",
		bargs.ba_cinfo.bkvo_port);
	fprintf(stderr, "\t-s           Use SSL [%s]\n",
		(bargs.ba_cinfo.bkvo_usetls?"yes":"no"));
	fprintf(stderr, "\t-u id        User ID [%lld]\n",
		bargs.ba_cinfo.bkvo_id);
	fprintf(stderr,	"\t-m pass      User Password [%s]\n",
		bargs.ba_cinfo.bkvo_pass);
	fprintf(stderr, "\t-q           Be quiet [yes]\n");
	fprintf(stderr, "\t-t           Be terse [no]\n");
	fprintf(stderr, "\t-v           Be verbose [no]\n");
	fprintf(stderr, "\t-y           Automatic yes to prompts [no]\n");
	fprintf(stderr, "\t-f filename  Replace stdin with filename;\n");
	fprintf(stderr, "\t                 forces interactive mode\n");
	fprintf(stderr, "\t-V           Show versions\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available CMD OPTIONS: %s CMD -?\n",
		bargs.ba_progname);
        exit(2);
}

// Yes or no user input
void
print_args(struct bargs *ba)
{
#define PA_LABEL_WIDTH  "12"
	printf("%" PA_LABEL_WIDTH "s kinetic%s://%lld:%s@%s:%s/%s\n", "URL:",
	       ba->ba_cinfo.bkvo_usetls?"s":"",
	       ba->ba_cinfo.bkvo_id, ba->ba_cinfo.bkvo_pass,
	       ba->ba_cinfo.bkvo_host, ba->ba_cinfo.bkvo_port, ba->ba_cmdstr);

	printf("%" PA_LABEL_WIDTH "s %s\n", "Host:", ba->ba_cinfo.bkvo_host);
	printf("%" PA_LABEL_WIDTH "s %s\n", "Port:", ba->ba_cinfo.bkvo_port);
	printf("%" PA_LABEL_WIDTH "s %lld\n","UserID:", ba->ba_cinfo.bkvo_id);
	printf("%" PA_LABEL_WIDTH "s %s\n", "UserPass:",
	       ba->ba_cinfo.bkvo_pass);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Use TLS:",
	       ba->ba_cinfo.bkvo_usetls);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Yes:", ba->ba_yes);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Quiet:", ba->ba_quiet);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Terse:", ba->ba_terse);
	printf("%" PA_LABEL_WIDTH "s %d\n", "Verbose:", ba->ba_verbose);
	printf("%" PA_LABEL_WIDTH "s %s\n", "Command:", ba->ba_cmdstr);
}

void
print_version()
{
	if (bkv_vers_num) /* if is just to get rid of compiler warnings */
		sprintf(bkv_vers, "%d.%d.%d",
			BKV_VERS_MAJOR, BKV_VERS_MINOR, BKV_VERS_PATCH);

	printf("BKV Utility Version: %s\n", bkv_vers);
}


int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int   optind, opterr, optopt;
	FILE *f;
	char         c, *cp;
	int          i, rc;

	bargs.ba_progname = argv[0];
	
	while ((c = getopt(argc, argv, "+c:f:h:m:p:qsu:tvVy?")) != EOF) {
		switch (c) {
		case 'h':
			bargs.ba_cinfo.bkvo_host = optarg;
			break;

		case 'm':
			bargs.ba_cinfo.bkvo_pass = optarg;
			break;

		case 'p':
			bargs.ba_cinfo.bkvo_port = optarg;
			break;

		case 'f':
			/* Reset stdin for interactive mode */
			if ((f = freopen(optarg, "r", stdin )) == NULL) {
				perror("open");
				usage();
			}
			bargs.ba_input = BKV_SCRIPT;
			break;

		case 's':
			bargs.ba_cinfo.bkvo_usetls = 1;
			break;

		case 'q':
			bargs.ba_quiet = 1;
			break;

		case 't':
			bargs.ba_terse = 1;
			break;

		case 'u':
			bargs.ba_cinfo.bkvo_id = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "*** Invalid ID %s\n", optarg);
				usage();
			}
			break;

		case 'v':
			bargs.ba_verbose = 1;
			break;

		case 'V':
			print_version();
			exit(0);

		case 'y':
			bargs.ba_yes = 1;
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
	if ((argc - optind == 0) || (bargs.ba_input != BKV_CMDLINE)) {
		if (bargs.ba_input ==  BKV_CMDLINE)
			bargs.ba_input =  BKV_INTERACTIVE;

		if (bargs.ba_verbose && (bargs.ba_input == BKV_INTERACTIVE))
			printf("Going interactive...\n");

		b_interactive(&bargs);
		exit(0);
	}

	/* consume cmd */
	bargs.ba_cmdstr = argv[optind++];

	/* Loop through the table and validate the command */
	for(i=0; i<BKV_EOT; i++) {
		if (btable[i].btab_cmd == BKV_EOT)
			break;

		if (strcmp(btable[i].btab_cmdstr, bargs.ba_cmdstr) == 0) {
			// Found a good command
			bargs.ba_cmd = btable[i].btab_cmd;
			break;
		}
	}

	if (i == BKV_EOT || (btable[i].btab_cmd == BKV_EOT)) {
		/* bad command */
		fprintf(stderr, "*** Bad command: %s\n", bargs.ba_cmdstr);
		usage();
	}

	if (bargs.ba_verbose && !bargs.ba_terse)
		print_args(&bargs);

	rc = bkv(argc, argv, &bargs);

	exit(rc);
}

int
b_start(struct bargs *ba)
{
	int ktd, rc;

	ktd = bkv_open(&ba->ba_cinfo);
	if (ktd < 0) {
		fprintf(stderr, "%s: Connection Failed\n", ba->ba_progname);
		return(-1);
	}

	rc = bkv_limits(ktd, &ba->ba_limits);
	if (rc < 0) {
		fprintf(stderr, "%s: Unable to get kinetic limits\n",
			ba->ba_progname);
		return(-1);
	}

	return(ktd);
}


int
bkv(int argc, char *argv[], struct bargs *ba)
{
	int i, ktd;
	int rc = -1;

	ktd = b_start(ba);
	if (ktd < 0) {
		fprintf(stderr, "%s: UNable to start\n", ba->ba_progname);
		return(EINVAL);
	}

	for (i = 0; i < BKV_EOT; i++) {
		if (btable[i].btab_cmd == bargs.ba_cmd) {
			/* Found a good command, call it */
			rc = (*btable[i].btab_handler)(argc, argv, ktd, ba);
			break;
		}
	}

	bkv_close(ktd);

	return(rc);
}

int
b_interactive(struct bargs *ba)
{
	enum { maxargs = 1024 };
	char *line, *sline, *p, *argv[maxargs];
	int i, j, rc, ktd, argc, nargc, unmatchedq;
	size_t len;
	ssize_t br;
	extern char     	*optarg;
        extern int		optind, opterr, optopt;

	ktd = b_start(ba);
	if (ktd < 0) {
		fprintf(stderr, "%s: Unable to start\n", ba->ba_progname);
		return(EINVAL);
	}

	while (1) {
		/* Either interactive mode or script mode */
		if (ba->ba_input == BKV_INTERACTIVE) {
			if ((line = readline("bkv> ")) == 0) {
				break;
			}
			/* dup the line for use in the history */
			add_history(line);
		} else {
			/* BKV_SCRIPT */
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

		if (ba->ba_input == BKV_SCRIPT) {
			printf("bkv> %s", argv[0]);
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

		if (ba->ba_verbose)
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
			print_args(ba);
			goto next;
		}

		if (strcmp(argv[0], "verbose") == 0) {
			if (ba->ba_verbose) {
				ba->ba_verbose = 0;
				printf("Verbose Mode in OFF\n");
			} else {
				ba->ba_verbose = 1;
				printf("Verbose Mode in ON\n");
			}
			goto next;
		}

		if (strcmp(argv[0], "version") == 0) {
			print_version();
		        goto next;
		}

		if (strcmp(argv[0], "yes") == 0) {
			if (ba->ba_yes) {
				ba->ba_yes = 0;
				printf("Yes Mode in OFF\n");
			} else {
				ba->ba_yes = 1;
				printf("Yes Mode in ON\n");
			}
			goto next;
		}

		optind = 0;
		ba->ba_cmdstr = argv[0];
		for(i=0; i<BKV_EOT; i++) {
			if (btable[i].btab_cmd == BKV_EOT)
				break;

			if (strcmp(btable[i].btab_cmdstr, ba->ba_cmdstr) == 0) {
				// Found a good command
				bargs.ba_cmd = btable[i].btab_cmd;

				rc = (*btable[i].btab_handler)(argc, argv,
							       ktd, ba);
				break;
			}
		}

		if ((i == BKV_EOT) ||(btable[i].btab_cmd == BKV_EOT))
			fprintf(stderr, "%s: Command not found\n", argv[0]);

	next:
		/* free the saved line ptr and go around again */
		for(i=0; i<argc; i++) {
			free(argv[i]);
		}
		free(sline);
	}

	if (ba->ba_verbose)
		printf("\nkctl exiting\n");

	bkv_close(ktd);

	return rc;
}

int
kctl_nohandler(int argc, char *argv[], int kts, struct bargs *ba)
{

	fprintf( stderr,  "Illegal call - Should never be called\n");
	return(-1);
}

