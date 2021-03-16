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

// ------------------------------
// Includes

#include "kfixtures.hpp"

extern "C" {
    #include <kinetic/kinetic.h>
}

// ------------------------------
// Globals

extern char *ki_status_label[];

namespace KFixtures {

    // ------------------------------
    // Globals for fixtures

    const char test_host[] = "127.0.0.1";
    const char test_port[] = "8123";
    const char test_hkey[] = "asdfasdf";

    void validate_status(kstatus_t cmd_status, kstatus_t exp_code) {
        EXPECT_EQ(cmd_status, exp_code);
    }

    void print_status(kstatus_t cmd_status) {
        fprintf(stdout,
            "Status Code (%d): %s\n", cmd_status, ki_error(cmd_status)
        );
    }

    // Constant test data used for keyval tests
    namespace TestDataKeyVal {

        /*
         * TODO
        const kv_t pak_key = (kv_t) {
            .kv_key           = struct kiovec[] { char[] {",
            .kv_keycnt        =,
            .kv_val           =,
            .kv_valcnt        =,
            .kv_ver           =,
            .kv_verlen        =,
            .kv_newver        =,
            .kv_newverlen     =,
            .kv_disum         =,
            .kv_disumlen      =,
            .kv_ditype        =,
            .kv_cpolicy       =,
            .kv_protobuf      =,
            .destroy_protobuf =,
        };
        char key_str[] = "pak";
        // expected value
        char     val_str[]         = "Hello World";
        size_t   val_len           = strlen(val_str);

        // expected checksum
        uint32_t val_checksum      = 0;
        Buffer disum_buffer = (Buffer) {
            .len  =          sizeof(uint32_t),
            .data = (void *) &val_checksum   ,
        };

        // expected db version
        char     existing_dbver[11];
        sprintf(existing_dbver, "0x%08x", val_checksum);
        Buffer dbver_buffer = (Buffer) {
            .len  = (size_t) 10                  ,
            .data = (void *) &(existing_dbver[0]),
        };

        // expected result (put it all together)
        TestHelpers::KVEntry *expected_entry = new TestHelpers::KVEntry();
        expected_entry->set_key    (&(key_str[0])         )
                      ->set_val    (&(val_str[0]), val_len)
                      ->with_dbver (&dbver_buffer         )
                      ->with_disum (&disum_buffer         )
                      ->with_ditype((kditype_t) KDI_CRC32 )
        ;
        */
    } // namespace TestDataKeyVal

} // namespace KFixtures
