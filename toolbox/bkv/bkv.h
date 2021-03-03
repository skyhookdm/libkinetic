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
#ifndef _BKV_H
#define _BKV_H

#define BKV_VERS_MAJOR 1
#define BKV_VERS_MINOR 0
#define BKV_VERS_PATCH 0

typedef enum bkv_cmd {
	BKV_NOOP = 0,
	BKV_GET,
	BKV_GETN,
	BKV_PUT,
	BKV_PUTN,
	BKV_DEL,
	BKV_EXISTS,
	BKV_LIMITS,

	BKV_EOT // End of Table -  Must be last
} bkv_cmd_t;

typedef enum bkv_input {
	BKV_INTERACTIVE,
	BKV_SCRIPT,
	BKV_CMDLINE,
} bkv_input_t;

struct bargs {
	char		*ba_progname;
	enum bkv_cmd	ba_cmd;		/* BKV_GETLOG, BKV_GET */
	char		*ba_cmdstr;     /* ex. "put", "get" */
	char		*ba_key;	/* raw unencoded key buffer */
	size_t		ba_keylen;	/* raw unencoded key buffer len */
	char		*ba_val;	/* raw unencoded value buffer */
	size_t		ba_vallen;	/* raw unencoded value buffer len */
	bkvs_open_t     ba_cinfo;	/* connection info */
	uint32_t	ba_quiet;	/* output ctl */
	uint32_t	ba_terse;	/* output ctl */
	uint32_t	ba_verbose;	/* output ctl */
	bkv_input_t	ba_input;	/* input mode */
	uint32_t	ba_yes;		/* answer yes to any prompts */
	bkv_limits_t	ba_limits;	/* BKV server limits */
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

#endif // _BKV_H
