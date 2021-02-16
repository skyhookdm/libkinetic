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
#ifndef __KFIXTURE_HPP
#define __KFIXTURE_HPP


#include <gtest/gtest.h>

extern "C" {
    #include <kinetic/kinetic.h>
}

namespace KFixtures {

    extern const char test_host[];
    extern const char test_port[];
    extern const char test_hkey[];

    struct context {
        const char    *host;        // connection host
        const char    *port;        // connection port, ex "8123"
        const char    *hkey;        // connection hmac key
        int64_t        user;        // connection user ID
        uint32_t       usetls;      // connection boolean to use TLS (8443)
        int64_t        clustervers; // Client cluster version number
        klimits_t      limits;      // Kinetic server limits
    };
    /*
     test_context = {
        .host        = test_host,
        .port        = test_port,
        .hkey        = test_hkey,

        .user        =  1,
        .usetls      =  0,
        .clustervers = -1,
    };
    */

	void validate_status(kstatus_t, kstatus_t);
    void print_status(kstatus_t);
}

#endif // __KFIXTURE_HPP
