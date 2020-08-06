#include "kinetic_internal.h"

const char *ki_error_msgs[] = {
	/* 0x00  0 */ "",
	/* 0x01  1 */ "Unable to allocate memory",
	/* 0x02  2 */ "Bad Session",
	/* 0x03  3 */ "Invalid Argument(s)",
	/* 0x04  4 */ "Unable to unpack kinetic message",
	/* 0x05  5 */ "Unable to unpack kinetic command",
	/* 0x06  6 */ "Message has no command data",
	/* 0x07  7 */ "Unable to construct kinetic request",
	/* 0x08  8 */ "Failed to receive message",
};
const int ki_errmsg_max = 9;
