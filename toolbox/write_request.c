#include <stdio.h>

// #include "KineticPrototype.h"
#include "config.h"
#include "kinetic/types.h"


int main(int argc, char **argv) {
    fprintf(
        stdout,
        "KineticPrototype Version: %d.%d\n",
        KineticPrototype_VERSION_MAJOR,
        KineticPrototype_VERSION_MINOR
    );

    /* connection options */
    KHeader  default_header;
    int64_t  version = 2, connectionid = 10;
    uint64_t timeout = 500;
    uint32_t batchid = 1;

    enum BuilderStatus init_status = kheader_initialize(
        &default_header,
        (struct KHeaderOptionalFields) {
            .clusterversion = &version,
            .connectionid   = &connectionid,
            .timeout        = &timeout,
            .batchid        = &batchid,

            .sequence   = NULL,
            .priority   = NULL,
            .timequanta = NULL
        }
    );

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

    enum BuilderStatus create_status = kinfo_create_request(&request_for_info, info_types, NULL);

    if (create_status == FAILURE) {
        fprintf(stderr, "Unable to create GetLog request\n");
        return EXIT_FAILURE;
    }

    /* construct message */
    KCommand kinetic_rpc;

    struct CommandResult marshal_result = kinfo_serialize_request(&kinetic_rpc, &default_header, &request_for_info);

    if (marshal_result.status == FAILURE) {
        fprintf(stderr, "Unable to pack Kinetic RPC\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Marshalled %ld bytes into a kinetic RPC\n", marshal_result.command_bytes.len);

    return EXIT_SUCCESS;
}
