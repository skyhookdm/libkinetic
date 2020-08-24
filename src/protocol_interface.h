/**
 * Copyright 2013-2015 Seagate Technology LLC.
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


#ifndef __PROTOCOL_INTERFACE_H
#define __PROTOCOL_INTERFACE_H

#include "kinetic_types.h"


// ------------------------------
// Macros for PDU (Protocol Data Unit) Structure

// Magic number and Packed length
#define KP_MAGIC 	0x46
#define KP_PLENGTH 9

// this macro unpacks a Kinetic PDU from a sequential set of KP_LENGTH bytes
#define UNPACK_PDU(_pdu, _p)	                                                \
	do {                                                                    \
		(_pdu)->kp_magic  = (uint8_t)(_p)[0];                           \
		                                                                \
		(_pdu)->kp_msglen = (uint32_t)(                                 \
			(_p)[4] << 24 | (_p)[3] << 16 | (_p)[2] << 8 | (_p)[1]  \
		);                                                              \
		(_pdu)->kp_msglen = ntohl((_pdu)->kp_msglen);                   \
		                                                                \
		(_pdu)->kp_vallen = (uint32_t)(                                 \
			(_p)[8] << 24 | (_p)[7] << 16 | (_p)[6] << 8 | (_p)[5]  \
		);                                                              \
		(_pdu)->kp_vallen = ntohl((_pdu)->kp_vallen);                   \
	} while(0);

// this macro packs a Kinetic PDU into a sequential set of KP_LENGTH bytes
#define PACK_PDU(_pdu, _p)	                     \
	do {                                         \
		uint32_t d;                          \
		(_p)[0] = (uint8_t)(_pdu)->kp_magic; \
		d = htonl((_pdu)->kp_msglen);        \
		(_p)[4] = (d & 0xff000000) >> 24;    \
		(_p)[3] = (d & 0x00ff0000) >> 16;    \
		(_p)[2] = (d & 0x0000ff00) >>  8;    \
		(_p)[1] = (d & 0x000000ff);          \
		d = htonl((_pdu)->kp_vallen);        \
		(_p)[8] = (d & 0xff000000) >> 24;    \
		(_p)[7] = (d & 0x00ff0000) >> 16;    \
		(_p)[6] = (d & 0x0000ff00) >>  8;    \
		(_p)[5] = (d & 0x000000ff);          \
	} while(0);


// ------------------------------
// conversion to and from protobuf structures
int keyname_to_proto(ProtobufCBinaryData *proto_keyval, struct kiovec *keynames, size_t keycnt);

char *helper_bytes_to_str(ProtobufCBinaryData proto_bytes);

void extract_to_command_header(kproto_cmdhdr_t *proto_cmdhdr, kcmdhdr_t *cmdhdr_data);

kstatus_t extract_cmdhdr(struct kresult_message *response_msg, kcmdhdr_t *cmdhdr_data);
kstatus_t extract_getlog(struct kresult_message *response_msg, kgetlog_t *getlog_data);
kstatus_t extract_getkey(struct kresult_message *response_msg, kv_t *kv_data);
kstatus_t extract_putkey(struct kresult_message *response_msg, kv_t *kv_data);
kstatus_t extract_delkey(struct kresult_message *response_msg, kv_t *kv_data);

kstatus_t extract_cmdstatus(kproto_cmd_t *protobuf_command);


// ------------------------------
// conversion to and from wire format
kproto_cmd_t        *unpack_kinetic_command(ProtobufCBinaryData commandbytes);
ProtobufCBinaryData  pack_kinetic_command(kproto_cmd_t *cmd_data);
ProtobufCBinaryData  create_command_bytes(kcmdhdr_t *cmd_hdr, void *proto_cmd_data);

enum kresult_code   pack_kinetic_message(kproto_msg_t *msg_data, void **msg_buffer, size_t *msg_size);
struct kresult_message unpack_kinetic_message(void *response_buffer, size_t response_size);
struct kresult_message create_message(kmsghdr_t *msg_hdr, ProtobufCBinaryData cmd_bytes);
struct kresult_message create_getlog_message(kmsghdr_t *, kcmdhdr_t *, kgetlog_t *);


// ------------------------------
// resource management
void destroy_command(void *unpacked_cmd);
void destroy_message(void *unpacked_msg);


// ------------------------------
// helpers for ktli (transport layer) to access specific message fields
uint64_t ki_getaseq(struct kiovec *msg, int msgcnt);
void     ki_setseq(struct kiovec *msg, int msgcnt, uint64_t seq);


// ------------------------------
// helpers for data processing
int compute_hmac(kproto_msg_t *msg_data, char *key, uint32_t key_len);





#endif // __PROTOCOL_INTERFACE_H
