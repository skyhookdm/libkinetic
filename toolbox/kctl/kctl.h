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
#ifndef _KCTL_H
#define _KCTL_H

#define KCTL_VERS_MAJOR 1
#define KCTL_VERS_MINOR 0
#define KCTL_VERS_PATCH 0

typedef enum kctl_cmd {
	KCTL_NOOP = 0,
	KCTL_GET,
	KCTL_GETNEXT,
	KCTL_GETPREV,
	KCTL_GETVERS,
	KCTL_RANGE,
	KCTL_PUT,
	KCTL_DEL,
	KCTL_GETLOG,
	KCTL_CLUSTER,
	KCTL_UPGRADE,
	KCTL_PIN,
	KCTL_DEVICE,
	KCTL_ACL,
	KCTL_BATCH,
	KCTL_STATS,
	KCTL_FLUSH,
	KCTL_EXEC,
	
	KCTL_EOT // End of Table -  Must be last
} kctl_cmd_t;

typedef enum kctl_input {
	KCTL_INTERACTIVE,
	KCTL_SCRIPT,
	KCTL_CMDLINE,
} kctl_input_t;

struct kargs {
	char		*ka_progname;
	enum kctl_cmd	ka_cmd;		/* KCTL_GETLOG, KCTL_GET */
	char		*ka_cmdstr;     /* ex. "info", "get" */
	char		*ka_key;	/* raw unencoded key buffer */
	size_t		ka_keylen;	/* raw unencoded key buffer len */
	char		*ka_val;	/* raw unencoded value buffer */
	size_t		ka_vallen;	/* raw unencoded value buffer len */
	int64_t		ka_user;	/* connection user ID  */
	char		*ka_pass;	/* connection user ID password */
	char		*ka_host;	/* connection host  */
	char 		*ka_port;	/* connection port, ex "8123", */
					/* 8443 (TLS), "kinetic" */
	uint32_t	ka_usetls;	/* connection boolean to use TLS */
	uint32_t	ka_timeout;	/* connection timeout */
	int64_t		ka_clustervers;	/* Client cluster version number, */
					/* must match server cluster version */
	kbatch_t	*ka_batch;	/* holds global batch ptr */
	uint32_t	ka_quiet;	/* output ctl */
	uint32_t	ka_terse;	/* output ctl */
	uint32_t	ka_verbose;	/* output ctl */
	kctl_input_t	ka_input;	/* input mode */
	uint32_t	ka_yes;		/* answer yes to any prompts */
	uint32_t	ka_stats;	/* collect stats */
	klimits_t	ka_limits;	/* Kinetic server limits */
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

/*
 * Time stamp subtraction to get an interval in microseconds
 * 	_me is the minuend and is a timespec structure ptr
 * 	_se is the subtrahend and is a timespec structure ptr
 * 	_d is the difference in microseconds, should be a uint64_t
 *	KCTL_BNS is 1B nanoseconds
 *	KCTL_MAXINTV is 24hrs in microseconds
 */
#define KCTL_BNS	 1000000000L
#define KCTL_MAXINTV	90000000000L
#define ts_sub(_me, _se, _d) {						\
	(_d)  = ((_me)->tv_nsec - (_se)->tv_nsec);			\
	if ((_d) < 0) {							\
		--(_me)->tv_sec;					\
		(_d) += KCTL_BNS;					\
	}								\
	(_d) += ((_me)->tv_sec - (_se)->tv_sec) * KCTL_BNS;		\
	(_d) /= (uint64_t)1000;						\
	if ((_d) > KCTL_MAXINTV || (_d) <= 0) {				\
		printf("KCTL TS CHK: (%lu, %lu) - (%lu, %lu) = %lu\n",	\
		       (_me)->tv_sec, (_me)->tv_nsec,			\
		       (_se)->tv_sec, (_se)->tv_nsec,			\
		       (_d));						\
		(_d) = 0;						\
	}								\
}

#endif // _KCTL_H
