#ifndef _KINETIC_H
#define _KINETIC_H


#include "protocol_types.h"
#include "message.h"


//Forward declarations of operation-specific structs used in the API
enum header_field_type;

struct kgetlog;
typedef struct kgetlog kgetlog_t;


// Types for interfacing with API
enum kresult_code {
	SUCCESS = 0,
	FAILURE    ,
};

struct kbuffer {
	size_t	len;
	void   *base;
};

struct kresult_buffer {
	enum kresult_code  result_code;
	size_t			   len;
	void			  *base;
};

struct kresult_message {
	enum kresult_code  result_code;
	void			  *result_message;
};

// Kinetic Status block
typedef struct kstatus {
	kstatus_code_t	ks_code;
	char		   *ks_message;
	char		   *ks_detail;
} kstatus_t;


/**
 * The API.
 */
int ki_open(char *host, char *port,
	    uint32_t usetls, int64_t id, void *hmac);
int ki_close(int ktd);

kstatus_t ki_setclustervers(int ktd, int64_t vers);

kstatus_t ki_get(int ktd, char *key, void **value,
				 char **vers, char **tag, kditype_t **di);

kstatus_t ki_getlog(int ktd, kgetlog_t *glog);


#endif /*  _KINETIC_H */
