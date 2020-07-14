#ifndef _KINETIC_H
#define _KINETIC_H


#include "protocol_types.h"


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
    size_t  len;
    void   *base;
};

struct kresult_buffer {
    enum kresult_code  result_code;
    size_t             len;
    void              *base;
};

struct kresult_message {
    enum kresult_code  result_code;
    void              *result_message;
};

// Kinetic Status block
typedef struct kstatus {
    kstatus_code_t  ks_code;
    char           *ks_message;
    char           *ks_detail;
} kstatus_t;


/** ktli session helper functions and data.
 *
 * These provide enough session info to abstract the ktli layer from the the kinetic protocol
 * structure. To accomplish this, a session must be preconfigured with:
 *  - (data)     length of response header (full message length extracted from header)
 *  - (function) logic to set the seqence number in an outbound request
 *  - (function) logic to extract the ackSequence number from an inbound response
 *  - (function) logic to return the expected response length (given a header buffer)
 */
struct kiovec;
struct ktli_helpers {
    int     kh_recvhdr_len;

    // functions
    int64_t (*kh_getaseq_fn)(struct kiovec *msg, int msgcnt);
    void    (*kh_setseq_fn)(struct kiovec *msg, int msgcnt, int64_t seq);
    int32_t (*kh_msglen_fn)(struct kiovec *msg_hdr);
};

/**
 * The API.
 */

/* for mental reference
struct kresult_message create_header(uint8_t header_fields_bitmap, ...);
struct kresult_message unpack_response(struct kbuffer response_buffer);
*/


kstatus_t ki_setclustervers(int ktd, int64_t vers);

kstatus_t ki_get(int ktd, char *key, void **value,
                 char **vers, char **tag, kditype_t **di);

kstatus_t ki_getlog(int ktd, kgetlog_t *glog);


#endif /*  _KINETIC_H */
