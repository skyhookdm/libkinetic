#ifndef _KCTL_H
#define _KCTL_H

/* 
 * Arbitrary number determined by empirical tests 
 * PAK: fix with Limits call 
 */
#define GETKEYRANGE_MAX_COUNT 800

enum kctl_command {
	KCTL_NOOP = 0,
	KCTL_GET,
	KCTL_GETNEXT,
	KCTL_GETPREV,
	KCTL_GETVERS,
	KCTL_RANGE,
	KCTL_PUT,
	KCTL_DEL,
	KCTL_GETLOG,
	KCTL_SETCLUSTERV,
	KCTL_SETLOCKPIN,
	KCTL_LOCK,
	KCTL_UNLOCK,
	KCTL_ACL,
	
	KCTL_EOT // End of Table -  Must be last
};

struct kargs {
	char		*ka_progname;
	kctl_command	ka_cmd;		// command, ex. info, get, put
	char		*ka_cmdstr;     // command ascii str, ex. "info", "get"
	char		*ka_key;	// key, raw unencoded buffer
	size_t		ka_keylen;	// key len, raw unencoded buffer
	char		*ka_val;	// value, raw unencoded buffer
	size_t		ka_vallen;	// value len, raw unencoded buffer
	int64t		ka_user;	// connection user ID 
	char		*ka_hmac;	// connection password for user ID used
	char		*ka_host;	// connection host 
	uint32t		ka_port;	// connection port, ex 8123 (nonTLS),
					// 8443 (TLS)
	uint32t		ka_usetls;	// connection boolean to use TLS
	unsigned int	ka_timeout;	// connection timeout
	int64_t		ka_clustervers;	// Client cluster version number,
					// must match server cluster version
	uint32t		ka_quiet;	// output ctl
	uint32t		ka_terse;	// output ctl
	uint32t		ka_verbose;	// output ctl
	uint32t		ka_yes;		// answer yes to any prompts
};

/**
 * Ask for user input on stdin, boolean answer. Only chars 'yYnN' and a newline
 * accepted as user answer. newline accepts the default answer.
 * const char *prompt; 			is the message to prompt the user with.
 * unsigned int default answer;  	is the default answer
 * unsigned int attempts;		is the max tries to get a valid answer
 * 					    if exhausted, default answer 
 * 					    is returned.
 */
extern int yorn(const char *, unsigned int, unsigned int);

/**
 * Dump a buffer as hex and ascii
 */
extern void hexdump(const void*, size_t);

/**
 * Dump a buffer as a encoded string with unprintable chars as "\HH" where
 * H is an ascii hex digit [0-9A-F]
 */
extern void asciidump(const void*, size_t);

/**
 * Decode ascii arbitrary hexadecimal value escape sequences and replace 
 * with binary representation.  Only escape sequences /xHH are supported. 
 * A new buffer is created and the original buffer is copied in and escape 
 * seqs decoded. The new buffer is returned including the size. 
 */
extern void * asciidecode(const void* , size_t, void**, size_t *);

#endif // _KCTL_H
