#ifndef __KFIXTURE_HPP
#define __KFIXTURE_HPP


#include <gtest/gtest.h>

extern "C" {
    #include <kinetic/kinetic.h>
}

namespace KFixtures {

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
    /*
     test_context = {
        .host        = test_host,
        .port        = test_port,
        .hkey        = test_hkey,

        .user        =  1,
        .usetls      =  0,
        .clustervers = -1,
    };
    */

    void validate_status(kstatus_t *, kstatus_code_t , char *, char *);
    void print_status(kstatus_t *);
}

#endif // __KFIXTURE_HPP
