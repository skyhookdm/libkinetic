// macro for constructing errors concisely
#define kstatus_err(kerror_code, ki_errtype, kerror_detail) ( \
	(kstatus_t) {                                             \
		.ks_code    = (kerror_code)  ,                        \
		.ks_message = (ki_error_msgs[(ki_errtype)]),          \
		.ks_detail  = (kerror_detail),                        \
	}                                                         \
)

enum ki_error_type {
	KI_ERR_MALLOC = 0,
	KI_ERR_BADSESS   ,
	KI_ERR_INVARGS   ,
};

const char *ki_error_msgs[] = {
	/* 0x00  0 */ "Unable to allocate memory",
	/* 0x01  1 */ "Bad Session",
	/* 0x02  2 */ "Invalid Argument(s)",
};
const int ki_msgtype_max = 48;
