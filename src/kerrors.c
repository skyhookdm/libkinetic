#include "kinetic_internal.h"

const char *ki_error_msgs[] = {
	/* KI_ERR_NOMSG     */ "",
	/* KI_ERR_MALLOC    */ "Unable to allocate memory",
	/* KI_ERR_BADSESS   */ "Bad Session",
	/* KI_ERR_INVARGS   */ "Invalid Argument(s)",
	/* KI_ERR_MSGPACK   */ "Unable to pack kinetic message",
	/* KI_ERR_MSGUNPACK */ "Unable to unpack kinetic message",
	/* KI_ERR_CMDUNPACK */ "Unable to unpack kinetic command",
	/* KI_ERR_NOCMD     */ "Message has no command data",
	/* KI_ERR_CREATEREQ */ "Unable to construct kinetic request",
	/* KI_ERR_RECVMSG   */ "Failed to receive message",
	/* KI_ERR_RECVPDU   */ "Unexpected PDU length",
	/* KI_ERR_PDUMSGLEN */ "Length mismatch: PDU != message + value",
	/* KI_ERR_BATCH     */ "General Batch Error",
};
const int ki_errmsg_max = 12;
