#include <stdio.h>

#include "test_kinetic.h"

const char test_host[] = "127.0.0.1";
const char test_port[] = "8123";
const char test_hkey[] = "asdfasdf";

struct context test_context = {
    .host        = test_host,
    .port        = test_port,
    .hkey        = test_hkey,

    .user        =  1,
    .usetls      =  0,
    .clustervers = -1,
};

int
start_connection() {
    int conn_desc = ki_open(
        (char *) test_context.host,
        (char *) test_context.port,
        test_context.usetls,
        test_context.user,
        (char *) test_context.hkey
    );

    if (conn_desc < 0) {
        fprintf(stderr, "Test Connection Failed\n");
        return -1;
    }

    test_context.limits = ki_limits(conn_desc);
    if (!test_context.limits.kl_keylen) {
        fprintf(stderr, "Failed to receive kinetic device limits\n");
        return -1;
    }

    return conn_desc;
}
