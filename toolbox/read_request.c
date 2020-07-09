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
    struct kresult_message unpack_result = unpack_info_response(response_data);

    if (unpack_result.result_code == FAILURE) {
        fprintf(stderr, "Unable to read Kinetic RPC\n");
        return EXIT_FAILURE;
    }

    kproto_command *response_command = (kproto_command *) unpack_result.result_message;
    struct kbuffer requested_info_types = {
        .len  = response_command->body->getlog->n_types,
        .base = response_command->body->getlog->types
    };

    fprintf(stdout, "Types in info request are:\n\t");
    for (int info_type_ndx = 0; info_type_ndx < requested_info_types.len; info_type_ndx++) {
        kproto_getlog_type info_type_choice = ((kproto_getlog_type *) requested_info_types.base)[info_type_ndx];

        switch (info_type_choice) {
            case UTIL_INFO_TYPE:
                fprintf(stdout, "Utilization, ");
                break;

            case CAPACITY_INFO_TYPE:
                fprintf(stdout, "Capacity, ");
                break;

            case DEVICE_LIMITS_INFO_TYPE:
                fprintf(stdout, "Limits, ");
                break;

            default:
                fprintf(stdout, "Unknown (%d), ", info_type_choice);
                break;
        }
    }

    fprintf(stdout, "\n");

    return EXIT_SUCCESS;
}
