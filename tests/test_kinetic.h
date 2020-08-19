#ifndef _TEST_KINETIC_H
#define _TEST_KINETIC_H

#include <kinetic/kinetic.h>

extern const char test_host[];
extern const char test_port[];
extern const char test_hkey[];

struct context {
    const char    *host;        // connection host
    const char    *port;        // connection port, ex "8123"
    const char    *hkey;        // connection hmac key
    int64_t        user;        // connection user ID
    uint32_t       usetls;      // connection boolean to use TLS (8443)
    int64_t        clustervers; // Client cluster version number
    klimits_t      limits;      // Kinetic server limits
};


int start_connection();

#endif // _TEST_KINETIC_H
