/**
 * Copyright 2013-2020 Seagate Technology LLC.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"


struct kresult_message create_delkey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr,
											 kv_t *cmd_data, kcachepolicy_t cache_opt,
											 int bool_shouldforce) {

	// declare protobuf structs on stack
	kproto_cmdhdr_t proto_cmd_header;
	kproto_kv_t     proto_cmd_body;

	com__seagate__kinetic__proto__command__key_value__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// extract from cmd_data into proto_cmd_body
	int extract_result = keyname_to_proto(&proto_cmd_body, cmd_data);
	if (extract_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	// to delete we need to specify the dbversion
	set_bytes_optional(&proto_cmd_body, dbversion, cmd_data->kv_dbvers, cmd_data->kv_dbverslen);

	// if force is specified, then the dbversion is essentially ignored.
	set_primitive_optional(&proto_cmd_body, force          , bool_shouldforce ? 1 : 0);
	set_primitive_optional(&proto_cmd_body, synchronization, cache_opt               );

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, &proto_cmd_body, KMT_DEL
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	free(proto_cmd_body.key.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}
