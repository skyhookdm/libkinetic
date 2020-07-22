#include <stdio.h>

#include "../../kinetic.h"
#include "../../kio.h"
#include "../../message.h"

char pdu[] = {
    0x46, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00,
};

 

char glmsg[] = {
    0x20, 0x01, 0x2a, 0x18, 0x08, 0x01, 0x12, 0x14,

	// The HMAC?
    0x78, 0x7c, 0x3c, 0x91, 0xd4, 0x0b, 0x9d, 0x26,
    0x83, 0x5a, 0x8b, 0x55, 0xce, 0x3d, 0x51, 0xa2,
    0x79, 0xfd, 0xad, 0x63,

	0x3a, 0x20, 0x0a, 0x0c,
    0x08, 0x00, 0x18, 0x80, 0xdd, 0xce, 0xae, 0x04,
    0x20, 0x00, 0x38, 0x18, 0x12, 0x10, 0x32, 0x0e,
    0x08, 0x00, 0x08, 0x01, 0x08, 0x02, 0x08, 0x03,
    0x08, 0x04, 0x08, 0x05, 0x08, 0x06,
};
    
struct kiovec gl[2] = {
    { .kiov_base = pdu,   .kiov_len = sizeof(pdu)},
    { .kiov_base = glmsg, .kiov_len = sizeof(glmsg)},
};

 

uint64_t seq = 0;
uint64_t clusterverion = 0;
uint64_t connectionid = 1171500672;


void
print_message(kproto_msg_t *proto_msg) {
	kproto_hmacauth_t *msg_hmac = proto_msg->hmacauth;
	printf("Message\n\tHMAC: [");

	for (int hmac_char_pos = 0; hmac_char_pos < msg_hmac->hmac.len; hmac_char_pos++) {
		if (hmac_char_pos > 0) { printf(" "); }

		printf("%x", msg_hmac->hmac.data[hmac_char_pos]);
	}

	printf("]");

	if (msg_hmac->has_identity) { printf("\n\tHMAC ID: %ld\n", msg_hmac->identity); }
	else { printf("\n"); }
}
 

int
main()
{
    printf("%lu %lu\n", gl[0].kiov_len, gl[1].kiov_len);

	// unpack and inspect the test data above
	struct kresult_message unpack_result = unpack_kinetic_message(gl[1].kiov_base, gl[1].kiov_len);
	if (unpack_result.result_code == FAILURE) {
		fprintf(stderr, "Unable to unpack message\n");
		return 1;
	}

	kproto_msg_t *unpacked_msg = unpack_result.result_message;
	if (!unpacked_msg->has_commandbytes) {
		fprintf(stderr, "Command bytes expected; but message has none\n");
		return 1;
	}

	printf("# ------------------------------\n# Test Message\n");
	print_message(unpacked_msg);

	// kproto_cmd_t *unpacked_cmd  = unpack_kinetic_command(unpacked_msg->commandbytes);


	// see if we can replicate the whole thing
	int hmac_result;

	hmac_result = compute_hmac(unpacked_msg, "asdfasdf", 8);
	printf(
		"# ------------------------------\n# First Processed Test Message (hmac result: %d)\n",
		hmac_result
	);
	print_message(unpacked_msg);

	hmac_result = compute_hmac(unpacked_msg, "asdfasdf", 8);
	printf(
		"# ------------------------------\n# Second Processed Test Message (hmac result: %d)\n",
		hmac_result
	);
	print_message(unpacked_msg);

	return 0;
}
