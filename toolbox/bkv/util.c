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
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

/** 
 * Dump a buffer in both hex. Sample output:
00000000 6C 61 73 66 6A 6C 61 73 64 6A 6C 61 6A 73 51 49  |  lasfjlasdjlajsQI 
00000010 51 49 51 49 09 09 09 09 09 6C 46 4C 6A 66 73 64  |  QIQI.....lFLjfsd 
00000020 6C 66 6A 61 6C 73 66 20 64 6C 6B 73 6A 66 6C 73  |  lfjalsf dlksjfls 
00000030 6A 66 6C 6B 6A 73 20 6C 6B 6A 73 66 6C 6A 6C 6B  |  jflkjs lkjsfljlk 
00000040 61 73 64 66 6B 6C 39 33 30 72 20 35 75 67 6A 67  |  asdfkl930r 5ugjg 
00000050 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |  ................
Last line repeated 3 time(s)
00000090 00 00 00 00 00 00 00 00 00 00 00 00              |  ................
 */
void
hexdump(const void* data, size_t size)
{
	char offset[10];	/* Byte Offset string */
	char line[81];		/* Current line */
	char lline[81];		/* Last line for comparison */
	char *sep = " |  ";	/* Seporator string */ 
	size_t lines, l, i, j, bpl=16, lb, bsl=3, lo, repeat = 0;

	/* 
	 * dump the output line by line. This way if lines are 
	 * repeated they can be detected and not printed again. 
	 * The hex portion is a little inefficient as it is O(x^2)
	 * but is simple to read. 
	 *
	 * bpl is Bytes per Line
	 * lb is Line Bytes, mostly lb = bpl except for last line
	 * bsl is Byte String Length, length of single byte in ascii + " "
	 * lo is Line Offset
	 */
	lines = (size / bpl) + ((size % bpl)?1:0);
	lline[0] = '\0';
	lb = bpl;
	for (l=0,i=0; l<lines; i+=lb, l++) {

		/* Last line update bpl if necessary */
		if (l == (lines - 1)) 
			if (size % bpl)
				lb = size % bpl;
		
		/* Offset */ 
		sprintf(offset, "%08x ", (unsigned int)(l * bpl));
		
		/* Hex dump */
		line[0] = '\0';
		for (j = 0; j < lb; ++j) {
			sprintf(line, "%s%02X ", line,
				((unsigned char*)data)[i+j]);
		} 

		/* Hex Ascii Separator, variable width */
		sprintf(line, "%s%*s", line,
			(int)(((bpl - lb) * bsl) + strlen(sep)), sep);
		
		/* Ascii dump */
		lo = strlen(line);
		for (j = 0; j < lb; ++j) {
			if (isprint(((unsigned char *)data)[i+j]))
				line[lo] = ((char *)data)[i+j];
			else
				line[lo] = '.';
			lo++;
		}
		line[lo] = '\0';

		/* check if repeated */
		if (strcmp(line, lline)) {
			/* Non repeated line, print it */
			if (repeat) 
				printf("    Last line repeated %ld time(s)\n",
				       repeat);

			printf("%s%s\n", offset, line);

			/* Save last line */
			strcpy(lline, line);
			repeat = 0;
		} else {
			/* Repeated, don't print just count */
			repeat++;
		}
	}

	if (repeat) 
		printf("Line repeated %ld time(s)\n", repeat);
}


/**
* print a buffer as an ascii string but where chars are unprintable replace
* then with a "\HH" notation where H is an ascii hexdigit.
* ex "O\x01--\x7F\xFF"
*/
void
asciidump(const void* data, size_t size)
{
	size_t i;
	
	for (i = 0; i < size; ++i) {
		if (isprint((int)(((unsigned char*)data)[i]))) {
			printf("%c", ((unsigned char*)data)[i]);
		} else {
			printf("\\x%02X", ((unsigned char*)data)[i]);
		}
	}
}

/** 
 * I pulled this together quickly to help with my debugging efforts.
 * This routine allocates a buffer to deposit the decode the data into.
 * Caller is responsible for freeing this buffer
 */
void *
asciidecode(const void* data, size_t size, void** rawdata, size_t *rawlen)
{
	char s[3] = "FF";
	char *cp;
	size_t i, j;

	/*
	 * Get a buffer. Converting \xHH chars into a single char means that the source buffer length
	 * should be more than needed. We add 1 to the length of the raw buffer (for a null byte) in
	 * case both of the following are true:
	 *		- the source buffer has no \xHH chars
	 *		- the source buffer is not null-terminated
	 */
	*rawdata = malloc(sizeof(char) * (size + 1));
	if (!*rawdata)
		return(NULL);

	// Use an appropriately typed alias for convenience
	char *rawdata_alias = (char *) *rawdata;

	/*
	 * j tracks the size of the raw buffer, i is used to track through
	 * passed in buffer.  
	 */
	for (j = 0, i = 0; i < size; ++i) {
		/* look for an encoded char: "\x" */
		int is_encoded_char = strncmp("\\x", &(((const char *) data)[i]), 2);

		if (is_encoded_char == 0)  {
			/* Found one: consume the "\x" */
			i += 2;
			
			/*
			 * copy the encoded byte in s, null termination
			 * after 2 chars happens when s[] is declared above,
			 * that is s[2] = '\0
			 */
			s[0] = ((const char *) data)[i++]; /* inc i to consume */
			s[1] = ((const char *) data)[i];   /* 'for' inc's i */

			// Convert and increment j
			rawdata_alias[j++] = (char) strtol(s, &cp, 16);

			if (!cp || *cp != '\0') {
				fprintf(stderr, "*** Invalid char in decode %s\n", s);
				free(*rawdata);
				*rawlen  = 0;
				*rawdata = NULL;

				return (NULL);
			}
		} else {
			/* just a regular char, copy it, inc j */
			rawdata_alias[j++] = ((const char *) data)[i];
		}
	}
	
	rawdata_alias[j] = '\0';
	*rawlen          = j;

	return *rawdata;
}

/**
 * Ask for user input on stdin, boolean answer. Only chars 'yYnN' and a 
 * newline accepted as user answer. newline accepts the default answer.
 * const char *prompt; 			is the message to prompt the user with.
 * unsigned int default answer;  	is the default answer
 * unsigned int attempts;		is the max tries to get a valid answer
 *					  if exhausted, def. answer is returned.
 * Yes or no user input
 */
int
yorn(const char *prompt, unsigned int default_ans, unsigned int attempts)
{
#define YNBUFLEN 1024
	char s[YNBUFLEN], *t;
	
	memset((void *)s, 0, YNBUFLEN);
	while (attempts--) {
		printf("%s", prompt); t = fgets(s, YNBUFLEN, stdin);
		/* EOF or other errors, get out of here */
		if (!t) break;
		
		/* Empty string but successful, try again */	  
		if (!strlen(s)) continue;
		
		/* Empty string take the default, char is \n */
		if ((strlen(s) == 1) && (s[0]=='\n')) return default_ans;
		
		/*
		 * If more than one char answer, try again,
		 * most answers will have \n char as well, account for it
		 */
		if ((strlen(s) - 1) != 1) continue;

		if (s[0]=='y' || s[0]=='Y') return 1; /* Yes answer */
		if (s[0]=='n' || s[0]=='N') return 0; /* No answer */

		/* Bad input try again loop around */
	}
	return default_ans;
}

