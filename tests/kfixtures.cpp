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

    // ------------------------------
    // Constant test data used for keyval tests

    /**
     * This namespace contains primitives, strings, and simple structs that are used for inputs and
     * outputs for a test case.
     */
    namespace TestData {

        // >> primitive types and primitive arrays
        const uint32_t empty_int           = 0;
        const char     initial_version[10] = "0x00000000";

        // >> structs (that fit on a single-line)
        const struct kiovec empty_val = (struct kiovec) {  0, NULL          };
        const struct kiovec pak_key   = (struct kiovec) {  3, "pak"         };
        const struct kiovec pak_val   = (struct kiovec) { 11, "hello world" };

        // >> multi-line variables
        const struct kiovec noexist_ver = (struct kiovec) {
            32, "getversion_doesnotexist_38928383"
        };
        const struct kiovec noexist_key = (struct kiovec) {
            29, "-ForSureThisisAUniqueKeyName-"
        };

    } // namespace TestData


    /**
     * This namespace should contain data structures representing input data for a test fixture.
     */
    namespace TestInputs {
        kv_t inputkv_pak = (kv_t) {
        };

    } // namespace TestInputs


    /**
     * This namespace should contain data structures representing **expected output** of a test
     * fixture.
     */
    namespace TestOutputs {

        const kv_t expectedkv_pak = (kv_t) {
            .kv_key           = (struct kiovec *) &pak_key,
            .kv_keycnt        = 1                         ,
            .kv_val           = (struct kiovec *) &pak_val,
            .kv_valcnt        = 1                         ,
            .kv_ver           = initial_version           ,
            .kv_verlen        = 10                        ,
            .kv_newver        = NULL                      ,
            .kv_newverlen     = 0                         ,
            .kv_disum         = (void *) &empty_int       ,
            .kv_disumlen      = sizeof(uint32_t)          ,
            .kv_ditype        = (kditype_t) KDI_CRC32     ,
            .kv_cpolicy       = (kcachepolicy_t) KC_WB    ,
            .kv_protobuf      = NULL                      ,
            .destroy_protobuf = NULL                      ,
        };

        const kv_t err_notfound_response = (kv_t) {
            .kv_key           = (struct kiovec *) &empty_val,
            .kv_keycnt        = 1                           ,
            .kv_val           = (struct kiovec *) &empty_val,
            .kv_valcnt        = 1                           ,
            .kv_ver           = NULL                        ,
            .kv_verlen        = 0                           ,
            .kv_newver        = NULL                        ,
            .kv_newverlen     = 0                           ,
            .kv_disum         = NULL                        ,
            .kv_disumlen      = 0                           ,
            .kv_ditype        = (kditype_t) KDI_CRC32       ,
            .kv_cpolicy       = (kcachepolicy_t) KC_WB      ,
            .kv_protobuf      = NULL                        ,
            .destroy_protobuf = NULL                        ,
        };

    } // namespace TestOutputs

} // namespace KFixtures
