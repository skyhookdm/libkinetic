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
#ifndef __GETLOG_HELPER_HPP
#define __GETLOG_HELPER_HPP


#include <stdio.h>
#include <inttypes.h>
#include <gtest/gtest.h>

extern "C" {
    #include <kinetic/kinetic.h>
}

namespace TestHelpers {
    struct GetLogHelper {
        kgetlog_t getlog_data;
        kgltype_t getlog_infotypes[10];

        GetLogHelper();

        void add_info_type(kgltype_t);

        void add_config();
        void add_capacity();
        void add_utilization();
        void add_temperatures();
        void add_statistics();
        void add_messages();
        void add_limits();
        void add_log();


        void validate_config_empty();
        void validate_config();

        void validate_capacity_empty();
        void validate_capacity();

        void validate_utilization_empty();
        void validate_utilization();

        void validate_temps_empty();
        void validate_temps();

        void validate_stats_empty();
        void validate_stats();

        void validate_msgs_empty();
        void validate_msgs();

        void print_config();
        void print_capacity();
    };
}

#endif // __GETLOG_HELPER_HPP
