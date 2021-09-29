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
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <kinetic/kinetic.h>
#include "kctl.h"
#include "mjson.h"

#define CMD_USAGE(_ka) kctl_acl_usage(_ka)

int  kacl_parse(char *aclfile, kacl_t **kacl, size_t *nkacl, struct kargs *ka);
void kacl_destroy(kacl_t *kacl, size_t nkacl);
void kacl_print(kacl_t *kacl, size_t nkacl);
void kacl_jsonhelp();
extern char mjson_copyright[];

void
kctl_acl_usage(struct kargs *ka)
{
	fprintf(
		stderr,
		"Usage: %s [..] %s [CMD OPTIONS] FILENAME\n",
		ka->ka_progname,
		ka->ka_cmdstr
	);

	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-j           Show JSON ACL file help/sample\n");
	fprintf(stderr, "\t-n           Dry run, read/dump ACLs to be set\n");
	fprintf(stderr, "\t-c           Show MicroJSON copyright\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

#define CMD_USAGE(_ka) kctl_acl_usage(_ka)


/* Set ACLS on a kinetic server */
int
kctl_acl(int argc, char *argv[], int ktd, struct kargs *ka)
{
 	extern char     *optarg;
        extern int	optind, opterr, optopt;
	int		dryrun=0;
        char		c, *fname;
	kstatus_t	krc = K_OK;
	kacl_t		*kacl;
	size_t		nkacl;

        while ((c = getopt(argc, argv, "h?cjn")) != (char)EOF) {
                switch (c) {
		case 'c':
			printf("%s", mjson_copyright);
			return(0);
			
		case 'j':
			kacl_jsonhelp();
			return(0);


		case 'n':
			dryrun = 1;
			break;

		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }

	if (!ka->ka_usetls) {
		fprintf(stderr, "*** Warning: Setting ACLs requires SSL/TLS\n");
	}

	// Check for correct operation
	if (argc - optind != 1) {
		fprintf(stderr, "*** Too many/too few args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	fname = argv[argc-1];

	kacl_parse(fname, &kacl, &nkacl, ka);
	if (dryrun) {
		return(0);
	}

	krc = ki_setacls(ktd, kacl, nkacl);
	if (krc != K_OK) {
		printf("kinetic setacls op failed: status=%d %s\n",
		       krc, ki_error(krc));
		return(-1);
	}

	kacl_destroy(kacl, nkacl);

	return(0);
}

/*
 * JSON Parser is the MicroJSON parser from https://gitlab.com/esr/microjson.
 * a copy was taken from commit: 18bd972a2d07fcc68a53a8209a52f85163987397.
 * A fixed KACL JSON language is defined and examples are in the code.
 * 
 * The following types and constants are for MJSON defined KACL language
 *
 * Some arbitrary limits
 * NOTE: that mjson has attr name and value length limits
 */ 
#define MAXSTR   JSON_VAL_MAX
#define MAXCOM	 JSON_VAL_MAX
#define MAXSCOPE 1024

/* 
 * These are the structures used to house the parsed json
 */
char comment[MAXCOM];

typedef struct jscope {
	uint32_t	js_off;
	char		js_subkey[MAXSTR];
	char		js_perm[MAXSTR];
	bool		js_tls;
} jscope_t;

typedef struct jacl {
	int32_t		ja_id;
	char            ja_pass[MAXSTR];
	uint32_t	ja_maxpri;
	int32_t		ja_nscope;
	jscope_t	ja_scope[MAXSCOPE];
} jacl_t;

/*
 * This table is used to convert from a text permission to the
 * libkinetic numeric value.
 */
struct jperm {
	char 	*jp_permstr;
	int32_t	jp_perm;
} jperm_table[] = {
	{"all",		-1},
	{"get",		KAPT_GET},
	{"put",		KAPT_PUT},
	{"del",		KAPT_DEL},
	{"range",	KAPT_RANGE},
	{"setup",	KAPT_SETUP},
	{"p2p",		KAPT_P2P},
	{"log",		KAPT_GETLOG},
	{"security",	KAPT_SECURITY},
	{"power",	KAPT_POWER},
	{"exec",	KAPT_EXEC},
	{NULL,0},
};

/* 
 * Main variable that is passed to mjson
 */ 
static jacl_t jacl;

/* 
 * mjson keeps track of the line number for error reporting
 */
extern int json_lineno;

/*
 * Contruct attribute structures for mjson to use
 */
static const struct json_attr_t ja_scope[] = {
	{"offset", t_uinteger,	STRUCTOBJECT(jscope_t, js_off)},
	{"subkey", t_string,	STRUCTOBJECT(jscope_t, js_subkey), .len=MAXSTR},
	{"perm",   t_string,	STRUCTOBJECT(jscope_t, js_perm),   .len=MAXSTR},
	{"tlsreq", t_boolean, 	STRUCTOBJECT(jscope_t, js_tls)},
	{NULL},
};

static 	const struct json_attr_t ja_acl[] = {
	{"comment",t_string,    .addr.string = comment,         .len=MAXCOM},
	{"id",	   t_integer,	.addr.integer = &jacl.ja_id, .dflt.integer=-1},	
	{"pass",   t_string,	.addr.string = jacl.ja_pass,    .len=MAXSTR},
	{"maxpri", t_uinteger,	.addr.uinteger = &jacl.ja_maxpri},
	{"scopes", t_array,	STRUCTARRAY(jacl.ja_scope, ja_scope,
					    &jacl.ja_nscope)},
	{NULL},
};


/**
 * void kacl_destroy(kacl_t *kacl, size_t kaclcnt)
 *
 * Go through the kacl structure and free everything,
 * NOTE: depends on ka_pass and scopes kas_subkey being allocated strings.
 */
void
kacl_destroy(kacl_t *kacl, size_t kaclcnt)
{
	int i, j;

	if (!kacl || !kaclcnt) {
		/* Nothing to do */
		return;
	}

	for(i=0; i<kaclcnt; i++) {
		if (kacl[i].kacl_pass) free(kacl[i].kacl_pass);

		if (kacl[i].kacl_scope) {
			for (j=0; j<kacl[i].kacl_scopecnt; j++) {
				if (kacl[i].kacl_scope[j].kas_subkey)
					free(kacl[i].kacl_scope[j].kas_subkey);
			}

			free(kacl[i].kacl_scope);
		}
	}

	free(kacl);

	return;
}

void
kacl_print(kacl_t *kacl, size_t kaclcnt)
{
	int i, j, k;
	kacl_scope_t *kas;
	
	if (!kacl || !kaclcnt) {
		/* Nothing to do */
		return;
	}

	printf("\n%5s %5s %6s %20s %12s\n",
	       "ACL","Index","ID","Password", "MaxPriority");
	printf("%5s %5s %6s %20s %12s %8s\n",
	       "SCOPE","Index","Offset","Subkey", "Permission", "TLSReq");
	for(i=0; i<kaclcnt; i++) {
		printf("\n%5s %5d %6ld %20s %12u\n",
		       "ACL", i,
		       kacl[i].kacl_id, (char *)kacl[i].kacl_pass,
		       kacl[i].kacl_maxpri);

		kas = kacl[i].kacl_scope;
		for (j=0; j<kacl[i].kacl_scopecnt; j++) {
			k=0;
			while (jperm_table[k].jp_permstr) {
				if (jperm_table[k].jp_perm == kas[j].kas_perm) {
					break;
				}
				k++;
			}
			printf("%5s %5d %6lu %20s %12s %8s\n",
			       "SCOPE", j,
			       kas[j].kas_offset, (char *)kas[j].kas_subkey,
			       jperm_table[k].jp_permstr,
			       (kas[j].kas_tlsreq?"true":"false"));
		}
	}

	return;
}

/**
 * int acl_parse(char *aclfile, kacl_t **kacl, size_t *nkacl,, struct kargs *ka)
 *
 * Takes a filename, opens, reads and then parses it for acls. Returns
 * a kacl_t array and count for use in libkinetic.
 *
 *  aclfile	a valid and accisble filename, that points to a file
 *		containing the JSON formatted kinetic ACLs.
 *  kacl	a pointer to an array of kacl_t. The array will be allocated
 * 		in this routine and returned.
 *  nkacl	a pointer to size_T which will be set with the number of
 *		parsed acls.
 *
 */
int
kacl_parse(char *aclfile, kacl_t **kacl, size_t *nkacl, struct kargs *kargs)
{
	int i, j, fd, rb, trb, rc, pfound, status = 0;
	char *input, *end, *cur;
	uint32_t maxkey;
	struct stat st;	
	kacl_t *ka;
	kacl_scope_t *kas;

	if (!kacl || !nkacl || !kargs)
		return(-1);

	maxkey = kargs->ka_limits.kl_keylen;

	/* Check file access */
	rc = stat(aclfile, &st);
	if (rc < 0) {
		perror("Can't stat file:");
		return(-1);
	}

	if (!S_ISREG(st.st_mode)) {
		errno = EINVAL;
		fprintf(stderr, "Not a regular file\n");
		return(-1);
	}

	fd = open(aclfile, O_RDONLY);
	if (fd < 0){
		perror("Can't open file:");
		return(-1);
	}

	/* Allocate the input buffer, no need to initialize */
	input = malloc(st.st_size);
	if (!input) {
		errno = ENOMEM;
		fprintf(stderr, "Can't allocate input buff\n");
		return(-1);
	}		

	/* Read the file in */
	trb = 0;
	while (trb < st.st_size) {
		if ((rb = read(fd, input, (st.st_size - trb))) <= 0) {
			perror("Read failed:");
			return(-1);
		}
		trb += rb;
	}

	*nkacl = 0;
	*kacl = NULL;

	/* Parse the input buffer one top level object at a time */
	cur = input;
	end = input + trb;
	while (cur < end ) {
		/* NOTE: no need to init jacl, mjson handles that. */
		status = json_read_object((const char *)cur, ja_acl,
					  (const char **)&cur);
		if (status != 0) {
			fprintf(stderr, "line %d: parse error: %s\n",
				json_lineno,
				json_error_string(status));
			goto kacl_perr;
		}

		/* 
		 * Make comments and acls  mutually exclusive
		 * NOTE: this check depends on mjson initializing the
		 * the attrs to their default values on each invocation.
		 * strings always set first char to '\0' and integers
		 * their defined default value, 0 if undefined.
		 */
		if ((comment[0] != '\0') &&
		    ((jacl.ja_id      != -1)   ||
		     (jacl.ja_pass[0] != '\0') ||
		     (jacl.ja_maxpri)            )) {
			fprintf(stderr, "line %d: No comments in an ACL sect\n",
				json_lineno);
			goto kacl_perr;
		}

		/* If just a comment, dump and move on */
		if (comment[0] != '\0') {
			continue;
		}

		/* Validate parsed acl data all fields must be present */
		if (jacl.ja_id == -1) {
			fprintf(stderr, "missing id before line %d\n",
				json_lineno);
			goto kacl_perr;
		}

		if (jacl.ja_pass[0] == '\0')  {
			fprintf(stderr, "missing pass before line %d\n",
				json_lineno);
			goto kacl_perr;
		}

		if ((jacl.ja_maxpri < KPT_LOWEST) ||
		    (jacl.ja_maxpri > KPT_HIGHEST)) {
			fprintf(stderr,
				"Bad or missing maxpri=%d before line %d\n",
				jacl.ja_maxpri, json_lineno);
			fprintf(stderr, "Maxpri must be >= %d and <= %d\n",
				KPT_LOWEST, KPT_HIGHEST);
			goto kacl_perr;
		}

		/* Allocate a new kacl array element */
		(*nkacl)++;
		*kacl = realloc(*kacl, sizeof(kacl_t) * (*nkacl));
		if (!*kacl) {
			fprintf(stderr, "Failed memory allocatio\n");
			return(-1);
		}

		/* 
		 * Store the parsed jacl data in the new kacl
		 * Dup the strings cause the string arrays are reused
		 */
		ka = &((*kacl)[(*nkacl) - 1]);
		ka->kacl_id       = jacl.ja_id;
		ka->kacl_pass     = strdup(jacl.ja_pass);
		ka->kacl_passlen  = strlen(jacl.ja_pass);
		ka->kacl_maxpri   = jacl.ja_maxpri;
		ka->kacl_scopecnt = jacl.ja_nscope;

		/* Allocate the scope array for this new ACL */
		ka->kacl_scope = calloc(jacl.ja_nscope, sizeof(kacl_scope_t));
		if (!ka->kacl_scope) {
			fprintf(stderr, "Failed memory allocatio\n");
			goto kacl_perr;
		}
		kas = ka->kacl_scope;

		/*
		 * Validate and grab the scope data
		 *
		 * It would be nice to require and validate that all
		 * four scope keywords are explicitly defined in scope
		 * input. But with mjson that really isn't possible without
		 * putting code into it. For example, strings default to ""
		 * so there is no way to differentiate a missing 'subkey'
		 * keyword and 'subkey'="". Tricks could be played with
		 * default string values like "NO SUBKEY" but then that string
		 * becomes an usuable keyword for subkey. So if there
		 * is no subkey, permstr, offset or tls put into the input
		 * the defaults are used.
		 */
		for (i = 0; i < jacl.ja_nscope; i++) {
			/* search the permstr in the perm table */
			j=0; pfound=0;
			while (jperm_table[j].jp_permstr) {
				if (!strcmp(jperm_table[j].jp_permstr,
					    jacl.ja_scope[i].js_perm)) {
					pfound = 1;
					break;
				}
				j++;
			}

			if (!pfound) {
				fprintf(stderr, "Bad perm=%s before line %d\n",
					jacl.ja_scope[i].js_perm, json_lineno);
				goto kacl_perr;
			}

			/* Validate key filter. Is offset too big */
			if (jacl.ja_scope[i].js_off > (maxkey - 1)) {
				fprintf(stderr,
					"Bad offset=%u before line %d\n",
					jacl.ja_scope[i].js_off, json_lineno);
				goto kacl_perr;
			}

			/* Does the subkey extends beyond the max key */
			if (strlen(jacl.ja_scope[i].js_subkey) >
			    (maxkey - jacl.ja_scope[i].js_off)) {
				fprintf(stderr,
					"subkey=%s too large before line %d\n",
					jacl.ja_scope[i].js_subkey,json_lineno);
				goto kacl_perr;
			}
			
			kas[i].kas_offset  = jacl.ja_scope[i].js_off;
			kas[i].kas_subkey  = strdup(jacl.ja_scope[i].js_subkey);
			kas[i].kas_subkeylen = strlen(kas[i].kas_subkey);
			kas[i].kas_perm    = jperm_table[j].jp_perm;
			kas[i].kas_tlsreq  = jacl.ja_scope[i].js_tls;
		}

	}

	if (kargs->ka_verbose) kacl_print(*kacl, *nkacl);
	return(0);

 kacl_perr:
	/* Something bad has happened destroy any work done and exit */
	kacl_destroy(*kacl, *nkacl);
	*kacl = NULL;
	*nkacl = 0;

	return(-1);
}

char acljsonhelp[] = "{ \"comment\":\"This is a comment.\"}\n\
{ \"comment\":\"They are ignored, must be < 2k bytes, and cannot be in an ACL\"}\n\
{ \"comment\":\"\n\
		    Kinetic ACls JSON File Format\n\
\n\
The file format for ACLs is defined as a set of ACLs, each with their own\n\
unique id and array of scopes. All acls must be set at one time. There is\n\
no way to edit ACLs, only overwrite the entire set. This ACL JSON file \n\
format permits the setting of all ACLs. The JSON formatting is as follows:\n\
\n\
An ACL is defined as four required, case sensitive, name value pairs:\n\
\n\
  { id:integer, pass:string, maxpri:unsigned integer, scopes: array }\n\
\n\
Where,\n\
	id:integer\n\
	The identification number of the user that this acl will regulate.\n\
	Ids must be unique no repeats. Required.\n\
\n\
	pass:string\n\
	The password used to authenticate the user id above. Required.\n\
\n\
	maxpri:unsigned integer\n\
	Maximum priority the user id can invoke in a request. Required.\n\
\n\
	scopes:array\n\
	An array of scope name value pairs. This array defines a set of\n\
	permissions and access requirements for the defined id,\n\
	to be applied to either a key operation that matches a key filter\n\
	string or other non-key based management activities. The number\n\
	of scopes per id is currently limited to 1024. One scope required.\n\
\n\
A scope is defined as four required, case sensitive, name value pairs:\n\
\n\
  { offset:unsigned integer, subkey:string, tlsreq:boolean, perm:string }\n\
\n\
Where,\n\
	offset:unsigned integer\n\
	This defines the offset in a key of a key value pair, where the \n\
	the subkey will be matched. It can not be larger than the maximum\n\
	key size, usually 1024. Defaults to 0.\n\
\n\
	subkey:string\n\
	This defines the string to be matched at the offset defined above. If\n\
	an operation uses a key that matches this filter, the scope will be\n\
	applied to validate the operation. An empty subkey matches all keys. \n\
	Example: Suppose keys U.k1, U.k2, and k3.U exist. An offset of 3 \n\
	and subkey of U will only match the last key. An offset of 0 and \n\
	a subkey of U will match the first two keys. An empty subkey will\n\
	match them all. Defaults to empty string.\n\
\n\
	tlsreq:boolean\n\
	If true, all requests must be delivered via a secured TLS connection\n\
	for the defined perm operations. If false, both unsecured and\n\
	secured connections can be used. Security has an implicit tlsreq:true\n\
	for all ids. Defaults to false.\n\
\n\
	perm:string\n\
	perm defines the permission granted by this scope. It must be one\n\
	of the following:\n\
		all|get|put|del|range|setup|p2p|log|security|power|exec\n\
	Required. \n\
\"}\n\
\n\
{ \"comment\":\"Admins - all permissions but require TLS\"}\n\
{ \"id\":1, \"pass\":\"password-id1\", \"maxpri\":9, \"scopes\":[\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"all\",  \"tlsreq\":true }]}\n\
{ \"id\":2, \"pass\":\"password-id2\", \"maxpri\":9, \"scopes\":[\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"all\",  \"tlsreq\":true }]}\n\
\n\
{ \"comment\":\"Users - restricted permissions\"}\n\
{ \"id\":100, \"pass\":\"password-id100\", \"maxpri\":7, \"scopes\":[\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"get\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"put\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"del\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"range\", \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"log\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"\", \"perm\":\"exec\",  \"tlsreq\":false } ]}\n\
{ \"id\":101, \"pass\":\"password-id101\", \"maxpri\":5, \"scopes\":[\n\
    { \"offset\":0, \"subkey\":\"ID-101:\", \"perm\":\"get\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"ID-101:\", \"perm\":\"put\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"ID-101:\", \"perm\":\"del\",   \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"ID-101:\", \"perm\":\"range\", \"tlsreq\":false },\n\
    { \"offset\":0, \"subkey\":\"ID-101:\", \"perm\":\"exec\",  \"tlsreq\":false } ]}\n";


void
kacl_jsonhelp()
{
	printf("%s", acljsonhelp);
	return;
}

