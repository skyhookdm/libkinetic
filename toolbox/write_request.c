#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../src/kinetic.h"
#include "../src/protocol_types.h"
#include "../src/getlog.h"


int main(int argc, char **argv) {
    fprintf(stdout, "KineticPrototype Version: 0.1\n");

    /* message header */
    kmsghdr_t message_auth = {
        .kmh_atype  = KA_PIN,
        .kmh_id     = 0,
        .kmh_pinlen = 4,
        .kmh_pin    = "0000",
    };

    /* connection options */
    uint8_t header_field_bitmap   = CLUST_VER | CONN_ID | TIMEOUT | BATCH_ID;
    struct kresult_message header_result = create_header(
        header_field_bitmap,
        (int64_t)    2,
        (int64_t)   10,
        (uint64_t) 500,
        (uint32_t)   1
    );

    if (header_result.result_code == FAILURE)  {
        fprintf(stderr, "Unable to create Header for kinetic Command\n");
        return EXIT_FAILURE;
    }

    /* create actual request */
    kgetlog_t *getlog_msg_data = (kgetlog_t *) malloc(sizeof(kgetlog_t));
    getlog_msg_data->kgl_typecnt = 3;
    getlog_msg_data->kgl_type    = (kgltype_t []) {
        KGLT_UTILIZATIONS, KGLT_CAPACITIES, KGLT_LIMITS
    };

    struct kresult_message create_result = create_getlog_message(
        &message_auth, (kcmd_hdr_t *) header_result.result_message, getlog_msg_data
    );

    if (create_result.result_code == FAILURE) {
        fprintf(stderr, "Unable to create GetLog request\n");
        return EXIT_FAILURE;
    }

    /* construct command buffer */
    struct kresult_buffer pack_result = pack_kinetic_message((kmsg_t *) create_result.result_message);

    if (pack_result.result_code == FAILURE) {
        fprintf(stderr, "Unable to pack Kinetic RPC\n");
        return EXIT_FAILURE;
    }

    /* Write packed bytes to file for now */
    int     output_fd     = open("request.kinetic", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ssize_t bytes_written = write(output_fd, pack_result.base, pack_result.len);

    if (bytes_written < 0) {
        fprintf(stderr, "Unable to write marshalled bytes to file: 'request.kinetic'\n");
        return EXIT_FAILURE;
    }

    if (close(output_fd) < 0) {
        fprintf(stderr, "Unable to close file: 'request.kinetic'\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Packed %ld bytes into a kinetic RPC\n", pack_result.len);


    return EXIT_SUCCESS;
}
