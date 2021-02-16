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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../src/protocol_types.h"
#include "../src/protocol_interface.h"


int main(int argc, char **argv) {
    fprintf(stdout, "KineticPrototype Version: 0.1\n");

    // hard coded buffer size for now
    size_t  buf_size      = 1024;
    char   *command_bytes = (char *) malloc(sizeof(char) * buf_size);

    // read command bytes from file
    int     input_fd   = open("request.kinetic", O_RDONLY);
    ssize_t bytes_read = read(input_fd, command_bytes, buf_size);

    if (bytes_read < 0) {
        fprintf(stderr, "Unable to read bytes from file: 'request.kinetic'\n");
        close(input_fd);

        return EXIT_FAILURE;
    }

    else if (close(input_fd) < 0) {
        fprintf(stderr, "Unable to close file: 'request.kinetic'\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Read %ld bytes from response.\n", bytes_read);

    /* Wrap command bytes */
    struct kbuffer response_data = {
        .len  = bytes_read,
        .base = (void *) command_bytes
    };

    /* deserialize message */
    struct kresult_message unpack_result = unpack_response(response_data);

    if (unpack_result.result_code == FAILURE) {
        fprintf(stderr, "Unable to read Kinetic RPC\n");
        return EXIT_FAILURE;
    }

    kproto_command *response_command = (kproto_command *) unpack_result.result_message;
    struct kbuffer requested_getlog_types = {
        .len  = response_command->body->getlog->n_types,
        .base = response_command->body->getlog->types
    };

    fprintf(stdout, "Types in getlog request are:\n\t");
    for (int getlog_type_ndx = 0; getlog_type_ndx < requested_getlog_types.len; getlog_type_ndx++) {
        kproto_getlog_type getlog_type_choice = ((kproto_getlog_type *) requested_getlog_types.base)[getlog_type_ndx];

        switch (getlog_type_choice) {
            case UTIL_GETLOG_TYPE:
                fprintf(stdout, "Utilization, ");
                break;

            case CAPACITY_GETLOG_TYPE:
                fprintf(stdout, "Capacity, ");
                break;

            case DEVICE_LIMITS_GETLOG_TYPE:
                fprintf(stdout, "Limits, ");
                break;

            default:
                fprintf(stdout, "Unknown (%d), ", getlog_type_choice);
                break;
        }
    }

    fprintf(stdout, "\n");

    return EXIT_SUCCESS;
}
