/**
 * Copyright 2020-2021 Seagate Technology LLC.
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 * https://mozilla.org/MP:/2.0/.
 *
 * This program is distributed in the hope that it will be useful,
 * but is provided AS-IS, WITHOUT ANY WARRANTY; including without
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
 * License for more details.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "../src/kinetic.h"
#include "../src/protocol_types.h"
#include "../src/message.h"
#include "../src/getlog.h"


struct kresult_message create_getlog_message(kmsghdr_t *, kcmdhdr_t *, kgetlog_t *);

int main(int argc, char **argv) {
	fprintf(stdout, "KineticPrototype Version: 0.1\n");

	/* message header */
	kmsghdr_t message_auth = {
		.kmh_atype	= KA_PIN,
		.kmh_id		= 0,
		.kmh_pinlen = 4,
		.kmh_pin	= "0000",
	};

	/* connection options */
	kcmdhdr_t cmdhdr_opts = (kcmdhdr_t) {
		.kch_clustvers = 2,
		.kch_connid    = 10,
		.kch_timeout   = 500,
		.kch_batid	   = 1,
		.kch_seq	   = 0,
		.kch_ackseq    = 0,
		.kch_type	   = 0,
		.kch_pri	   = 0,
		.kch_quanta    = 0,
		.kch_qexit	   = 0,
	};

	/* create actual request */
	kgetlog_t cmdbody_data;
	memset((void *) &cmdbody_data, 0, sizeof(kgetlog_t));

	cmdbody_data.kgl_typecnt = 3;
	cmdbody_data.kgl_type	 = (kgltype_t []) {
		KGLT_UTILIZATIONS, KGLT_CAPACITIES, KGLT_LIMITS
	};

	struct kresult_message create_result = create_getlog_message(
		&message_auth, &cmdhdr_opts, &cmdbody_data
	);

	if (create_result.result_code == FAILURE) {
		fprintf(stderr, "Unable to create GetLog request\n");
		return EXIT_FAILURE;
	}

	/* construct command buffer */
	void   *msg_buffer;
	size_t msg_size;

	int pack_result = pack_kinetic_message(
		(kproto_msg_t *) create_result.result_message, &msg_buffer, &msg_size
	);

	if (pack_result == FAILURE) {
		fprintf(stderr, "Unable to pack Kinetic RPC\n");
		return EXIT_FAILURE;
	}

	/* Write packed bytes to file for now */
	int		output_fd	  = open("request.kinetic", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	ssize_t bytes_written = write(output_fd, msg_buffer, msg_size);

	if (bytes_written < 0) {
		fprintf(stderr, "Unable to write marshalled bytes to file: 'request.kinetic'\n");
		return EXIT_FAILURE;
	}

	if (close(output_fd) < 0) {
		fprintf(stderr, "Unable to close file: 'request.kinetic'\n");
		return EXIT_FAILURE;
	}

	fprintf(stdout, "Packed %ld bytes into a kinetic RPC\n", msg_size);


	return EXIT_SUCCESS;
}
