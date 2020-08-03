#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
/** 
* Dump a buffer in both hex. Sample output:
* 6C 61 73 66 6A 6C 61 73  64 6A 6C 61 6A 73 51 49  |  lasfjlasdjlajsQI 
* 51 49 51 49 09 09 09 09  09 6C 46 4C 6A 66 73 64  |  QIQI.....lFLjfsd 
* 6C 66 6A 61 6C 73 66 20  64 6C 6B 73 6A 66 6C 73  |  lfjalsf dlksjfls 
* 6A 66 6C 6B 6A 73 20 6C  6B 6A 73 66 6C 6A 6C 6B  |  jflkjs lkjsfljlk 
* 61 73 64 66 6B 6C 39 33  30 72 20 35 75 67 6A 67  |  asdfkl930r 5ugjg 
* 66 20 0A    
*/ 
void
hexdump(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	printf("%06x ", 0);
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		/* PAK: isprint? */
		if (((unsigned char*)data)[i] >= ' ' &&
		    ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n%06lx ", ascii, i+1);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
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
	 * Get a buffer. since this converts \xHH chars int a single char
	 * using the source buffer length is more than needed. 
	 */
	*rawdata = malloc(size);
	if (!*rawdata)
		return(NULL);

	/*
	 * j tracks the size of the raw buffer, i is used to track through
	 * passed in buffer.  
	 */
	for (j=0, i = 0; i < size; ++i) {
		/* look for an encoded char: "\x" */
		if (strncmp("\\x", &(((const char *)data)[i]), 2) == 0)  {
			/* Found one: consume the "\x" */
			i += 2;
			
			/*
			 * copy the encoded byte in s, null termination
			 * after 2 chars happens when s[] is declared above,
			 * that is s[2] = '\0
			 */
			s[0] = ((const char *)data)[i++]; /* inc i to consume */
			s[1] = ((const char *)data)[i];   /* 'for' inc's i */

			// Convert and increment j
			((char *)(*rawdata))[j++] = (char)strtol(s, &cp, 16);
			if (!cp || *cp != '\0') {
				fprintf(stderr,
					"*** Invalid char in decode %s\n", s);
				free(*rawdata);
				*rawlen = 0;
				*rawdata = NULL;
				return (NULL);
			}
		} else {
			/* just a regular char, copy it, inc j */
			((char *)(*rawdata))[j++] = ((const char *)data)[i];
		}
	}
	
	((char *)(*rawdata))[j] = '\0';
	*rawlen = j;
	return(*rawdata);
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

