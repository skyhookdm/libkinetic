#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../src/types.h"
#include "../src/kinetic.h"


int main(int argc, char **argv) {
    fprintf(stdout, "KineticPrototype Version: 0.1\n");

    /* connection options */
    KHeader  default_header;
    int64_t  version = 2, connectionid = 10;
    uint64_t timeout = 500;
    uint32_t batchid = 1;

    struct KHeaderOptionalFields header_options = {
        .clusterversion = &version,
        .connectionid   = &connectionid,
        .timeout        = &timeout,
        .batchid        = &batchid,

        .sequence   = NULL,
        .priority   = NULL,
        .timequanta = NULL
    };

    enum BuilderStatus init_status = kheader_initialize(&default_header, header_options);

    if (init_status == FAILURE)  {
        fprintf(stderr, "Unable to initialize Header for kinetic Command\n");
        return EXIT_FAILURE;
    }

    /* create actual request */
    KInfo request_for_info;
    ProtobufCBinaryData info_types = {
        .len  = 3,
        .data = (uint8_t []) {
            COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__UTILIZATIONS,
            COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__CAPACITIES  ,
            COM__SEAGATE__KINETIC__PROTO__COMMAND__GET_LOG__TYPE__LIMITS 
        }
    };

    com__seagate__kinetic__proto__command__get_log__init(&request_for_info);
    enum BuilderStatus create_status = kinfo_create_request(&request_for_info, info_types, NULL);

    if (create_status == FAILURE) {
        fprintf(stderr, "Unable to create GetLog request\n");
        return EXIT_FAILURE;
    }

    /* construct message */
    struct KineticRequest marshal_result = kinfo_serialize_request(&default_header, &request_for_info);

    if (marshal_result.status == FAILURE) {
        fprintf(stderr, "Unable to pack Kinetic RPC\n");
        return EXIT_FAILURE;
    }

    int output_fd = open("request.kinetic", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ssize_t bytes_written = write(
        output_fd,
        marshal_result.command_bytes.data,
        marshal_result.command_bytes.len
    );

    if (bytes_written < 0) {
        fprintf(stderr, "Unable to write marshalled bytes to file: 'request.kinetic'\n");
        return EXIT_FAILURE;
    }

    if (close(output_fd) < 0) {
        fprintf(stderr, "Unable to close file: 'request.kinetic'\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Marshalled %ld bytes into a kinetic RPC\n", marshal_result.command_bytes.len);


    return EXIT_SUCCESS;
}
