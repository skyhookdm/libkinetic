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
#include "kfixtures.hpp"

extern char *ki_status_label[];

namespace KFixtures {

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
}
