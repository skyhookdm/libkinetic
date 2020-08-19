#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

// kinetic.h imports getlog.h
#include <kinetic/kinetic.h>

#include "../test_kinetic.h"

void helper_add_config(kgetlog_t *glog) {
	glog->kgl_type[glog->kgl_typecnt++] = KGLT_CONFIGURATION;
}

void test_getlog_1() {
	// initialize getlog structure
	kgetlog_t glog;
	memset((void *) &glog, 0, sizeof(kgetlog_t));

	// populate getlog structure
	kgltype_t glt[10];
	glog.kgl_type    = glt;
	glog.kgl_typecnt = 0;

	helper_add_config(&glog);

    int conn_desc = start_connection();
	kstatus_t command_status = ki_getlog(conn_desc, &glog);

    if (command_status.ks_code != K_OK) {
        fprintf(stderr, "Error\n");
    }

    ki_close(conn_desc);
}

int main(int argc, char **argv) {
    return 0;
}
