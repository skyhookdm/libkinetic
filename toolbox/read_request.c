#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../src/types.h"
#include "../src/kinetic.h"


int main(int argc, char **argv) {
    fprintf(stdout, "KineticPrototype Version: 0.1\n");

    // hard coded buffer size for now
    size_t  buf_size      = 1024;
    char   *command_bytes = (char *) malloc(sizeof(char) * buf_size);

    int input_fd       = open("request.kinetic", O_RDONLY);
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
    ProtobufCBinaryData response_data = {
        .len  = bytes_read,
        .data = (uint8_t *) command_bytes
    };

    /* deserialize message */
    struct KineticResponse read_result = kinfo_deserialize_response(response_data);

    if (read_result.status == FAILURE) {
        fprintf(stderr, "Unable to read Kinetic RPC\n");
        return EXIT_FAILURE;
    }

    ProtobufCBinaryData info_types_requested = {
        .len  = read_result.command_response->body->getlog->n_types,
        .data = (uint8_t *) read_result.command_response->body->getlog->types
    };

    fprintf(stdout, "Types in info request are:\n\t");
    for (int info_type_ndx = 0; info_type_ndx < info_types_requested.len; info_type_ndx++) {
        uint8_t info_type_choice = info_types_requested.data[info_type_ndx];

        switch (info_type_choice) {
            case COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__UTILIZATIONS:
                fprintf(stdout, "Utilization, ");
                break;

            case COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__CAPACITIES:
                fprintf(stdout, "Capacity, ");
                break;

            case COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__LIMITS:
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
