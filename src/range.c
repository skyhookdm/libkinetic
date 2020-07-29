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

struct kresult_message create_rangekey_message(kmsghdr_t *, kcmdhdr_t *, keyrange_t *);
kstatus_t extract_keyrange(struct kresult_message *response_msg, keyrange_t *keyrange_data);

/**
 * ki_validate_kv(kv_t *kv, limit_t *lim)
 *
 *  kv		Always contains a key, but optionally may have a value
 *		However there must always value kiovec array of at least
 * 		element and should always be initialized. if unused,
 *		as in a get, kv_val[0].kiov_base=NULL and kv_val[0].kiov_len=0
 *  lim 	Contains server limits
 *
 * Validate that the user is passing a valid kv structure
 *
 */
int
ki_validate_range(kv_t *kv, klimits_t *lim)
{
	int i;
	size_t len;
	//int util, temp, cap, conf, stat, mesg, lim, log;

	/* assume we will find a problem */
	errno = K_EINVAL;

	/* Check for required params. This is _True_ on _error_  */
	int bool_has_invalid_params = (
		   !kv                                 // kv_t struct
		|| !kv->kv_key || kv->kv_keycnt < 1    // key name
		|| !kv->kv_val || kv->kv_valcnt < 1    // key value
		|| !kv->kv_curver                      // db version
		|| kv->kv_curverlen < 1                // db version is not too short
		|| kv->kv_curverlen > lim->kl_verlen   // db version is not too long
	);

	// db version must be provided, and must be a valid length
	if (bool_has_invalid_params) { return (-1); }

	/* Check key name and key value lengths are not too long */
	size_t total_key_len = calc_total_len(kv->kv_key, kv->kv_keycnt);
	if (total_key_len > lim->kl_keylen) { return(-1); }

	/* Total up the length across all vectors */
	size_t total_val_len = calc_total_len(kv->kv_val, kv->kv_valcnt);
	if (total_val_len > lim->kl_vallen) { return(-1); }

	errno = 0;

	return (0);
}

struct kresult_message create_rangekey_message(kmsghdr_t *msg_hdr, kcmdhdr_t *cmd_hdr, keyrange_t *cmd_data) {
	// declare protobuf structs on stack
	kproto_cmdhdr_t   proto_cmd_header;
	kproto_keyrange_t proto_cmd_body;

	com__seagate__kinetic__proto__command__range__init(&proto_cmd_body);

	// populate protobuf structs
	extract_to_command_header(&proto_cmd_header, cmd_hdr);

	// extract from cmd_data into proto_cmd_body
	int extract_startkey_result = keyname_to_proto(
		&(proto_cmd_body.startkey), cmd_data->start_key, cmd_data->start_keycnt
	);
	proto_cmd_body.has_startkey = extract_startkey_result;

	if (extract_startkey_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	int extract_endkey_result = keyname_to_proto(
		&(proto_cmd_body.startkey), cmd_data->end_key, cmd_data->end_keycnt
	);
	proto_cmd_body.has_endkey = extract_endkey_result;

	if (extract_endkey_result < 0) {
		return (struct kresult_message) {
			.result_code    = FAILURE,
			.result_message = NULL,
		};
	}

	set_primitive_optional(&proto_cmd_body, startkeyinclusive, cmd_data->bool_is_start_inclusive);
	set_primitive_optional(&proto_cmd_body, endkeyinclusive  , cmd_data->bool_is_end_inclusive  );
	set_primitive_optional(&proto_cmd_body, reverse          , cmd_data->bool_reverse_keyorder  );
	set_primitive_optional(&proto_cmd_body, maxreturned      , cmd_data->max_result_size        );

	// construct command bytes to place into message
	ProtobufCBinaryData command_bytes = create_command_bytes(
		&proto_cmd_header, &proto_cmd_body, KMT_GETRANGE
	);

	// since the command structure goes away after this function, cleanup the allocated key buffer
	// (see `keyname_to_proto` above)
	free(proto_cmd_body.startkey.data);
	free(proto_cmd_body.endkey.data);

	// return the constructed getlog message (or failure)
	return create_message(msg_hdr, command_bytes);
}

void destroy_protobuf_keyrange(keyrange_t *keyrange_data) {
	if (!keyrange_data) { return; }

	// first destroy the allocated memory for the message data
	destroy_command((kproto_keyrange_t *) keyrange_data->keyrange_protobuf);

	// then free arrays of pointers that point to the message data
	KI_FREE(keyrange_data->result_keys);

	// free the struct itself last
	// NOTE: we may want to leave this to a caller?
	KI_FREE(keyrange_data);
}

kstatus_t extract_keyrange(struct kresult_message *response_msg, keyrange_t *keyrange_data) {
	// assume failure status
	kstatus_t kv_status = (kstatus_t) {
		.ks_code    = K_INVALID_SC,
		.ks_message = NULL,
		.ks_detail  = NULL,
	};

	// commandbytes should exist, but we should probably be thorough
	kproto_msg_t *keyrange_response_msg = (kproto_msg_t *) response_msg->result_message;
	if (!keyrange_response_msg->has_commandbytes) { return kv_status; }

	// unpack the command bytes
	kproto_cmd_t *response_cmd = unpack_kinetic_command(keyrange_response_msg->commandbytes);
	if (response_cmd->body->range == NULL) { return kv_status; }

	// extract the response status to be returned. prepare this early to make cleanup easy
	kv_status = extract_cmdstatus(response_cmd);

	// check if there's any range information to parse
	if (response_cmd->body->range == NULL) { return kv_status; }

	// ------------------------------
	// begin extraction of command body into keyrange_t structure
	kproto_keyrange_t *response = response_cmd->body->range;

	keyrange_data->result_keycnt = response->n_keys;
	keyrange_data->result_keys   = (struct kiovec *) malloc(sizeof(struct kiovec) * response->n_keys);

	for (size_t result_keyndx = 0; result_keyndx < response->n_keys; result_keyndx++) {
		keyrange_data->result_keys[result_keyndx].kiov_base = response->keys[result_keyndx].data;
		keyrange_data->result_keys[result_keyndx].kiov_len  = response->keys[result_keyndx].len;
	}

	// set fields used for cleanup
	keyrange_data->keyrange_protobuf = response_cmd;
	keyrange_data->destroy_protobuf  = destroy_protobuf_keyrange;

	return kv_status;
}
