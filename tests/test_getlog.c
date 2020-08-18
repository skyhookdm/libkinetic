#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

// kinetic.h imports getlog.h
#include <kinetic/kinetic.h>

void helper_add_config(kgetlog_t *glog) {
	kstatus_t 	kstatus;

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
}
