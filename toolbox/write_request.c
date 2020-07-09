#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../src/protocol_types.h"
#include "../src/protocol_interface.h"


int main(int argc, char **argv) {
    fprintf(stdout, "KineticPrototype Version: 0.1\n");

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
    struct kbuffer info_types_buffer = {
        .len  = 3,
        .base = (uint8_t []) {
            UTIL_INFO_TYPE         ,
            CAPACITY_INFO_TYPE     ,
            DEVICE_LIMITS_INFO_TYPE,
        }
    };

    struct kresult_message getlog_result = create_info_request(info_types_buffer, NULL);
    if (getlog_result.result_code == FAILURE) {
        fprintf(stderr, "Unable to create GetLog request\n");
        return EXIT_FAILURE;
    }

    /* construct command buffer */
    struct kresult_buffer pack_result = pack_info_request(
        (kproto_header *const) header_result.result_message,
        (kproto_getlog *const) getlog_result.result_message
    );

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
