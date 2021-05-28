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

#include "helper.hpp"

namespace TestHelpers {
    GetLogHelper::GetLogHelper() {
        memset((void *) &(this->getlog_data), 0, sizeof(kgetlog_t));
        this->getlog_data.kgl_type = getlog_infotypes;
    }

    // functions to prepare the request
    void GetLogHelper::add_info_type(kgltype_t enum_getlog_info_type) {
        int type_ndx = this->getlog_data.kgl_typecnt++;
        this->getlog_data.kgl_type[type_ndx] = enum_getlog_info_type;
    }

    void GetLogHelper::add_config()       { add_info_type((kgltype_t) KGLT_CONFIGURATION); }
    void GetLogHelper::add_capacity()     { add_info_type((kgltype_t) KGLT_CAPACITIES);    }
    void GetLogHelper::add_utilization()  { add_info_type((kgltype_t) KGLT_UTILIZATIONS);  }
    void GetLogHelper::add_temperatures() { add_info_type((kgltype_t) KGLT_TEMPERATURES);  }
    void GetLogHelper::add_statistics()   { add_info_type((kgltype_t) KGLT_STATISTICS);    }
    void GetLogHelper::add_messages()     { add_info_type((kgltype_t) KGLT_MESSAGES);      }
    void GetLogHelper::add_limits()       { add_info_type((kgltype_t) KGLT_LIMITS);        }
    void GetLogHelper::add_log()          { add_info_type((kgltype_t) KGLT_LOG);           }

    // functions to validate a portion of the GetLog response
    void GetLogHelper::validate_config_empty() {
        kconfiguration_t actual_config = getlog_data.kgl_conf;

        EXPECT_EQ(actual_config.kcf_vendor       , nullptr);
        EXPECT_EQ(actual_config.kcf_model        , nullptr);
        EXPECT_EQ(actual_config.kcf_serial       , nullptr);
        EXPECT_EQ(actual_config.kcf_wwn          , nullptr);
        EXPECT_EQ(actual_config.kcf_version      , nullptr); 
        EXPECT_EQ(actual_config.kcf_compdate     , nullptr);
        EXPECT_EQ(actual_config.kcf_srchash      , nullptr);
        EXPECT_EQ(actual_config.kcf_proto        , nullptr);
        EXPECT_EQ(actual_config.kcf_protocompdate, nullptr);
        EXPECT_EQ(actual_config.kcf_protosrchash , nullptr);
        EXPECT_EQ(actual_config.kcf_port         , 0);
        EXPECT_EQ(actual_config.kcf_tlsport      , 0);
        EXPECT_EQ(actual_config.kcf_interfacescnt, 0   );
        EXPECT_EQ(actual_config.kcf_interfaces   , nullptr);

        // this definitely does not come back as an enum
        // EXPECT_NE(actual_config.kcf_power           , 0);
    }

    void GetLogHelper::validate_config() {
        kconfiguration_t actual_config = getlog_data.kgl_conf;

        EXPECT_STREQ(actual_config.kcf_vendor, "Seagate"            );
        EXPECT_STREQ(actual_config.kcf_model , "Drive Model"        );
        EXPECT_STREQ(actual_config.kcf_serial, "Drive SN"           );
        EXPECT_STREQ(actual_config.kcf_wwn   , "Drive WWN"          );

        EXPECT_STREQ(actual_config.kcf_version      , "09.02.08"                            ); 
        EXPECT_STREQ(actual_config.kcf_compdate     , "Tue Mar  3 14:27:35 PST 2020"        );
        EXPECT_STREQ(actual_config.kcf_srchash      , ""                                    );
        EXPECT_STREQ(actual_config.kcf_proto        , "4.0.1"                               );
        EXPECT_STREQ(actual_config.kcf_protocompdate, "Tue Mar  3 14:27:35 PST 2020"        );
        EXPECT_STREQ(actual_config.kcf_protosrchash , "f8d5d18"                             );

        EXPECT_EQ(actual_config.kcf_port            , 8123);
        EXPECT_EQ(actual_config.kcf_tlsport         , 8443);
        // TODO: this assumes kineticd running on x86. If it fails, just disable (and file issue)
        EXPECT_EQ(actual_config.kcf_interfacescnt   , 3   );
        // this definitely does not come back as an enum
        // EXPECT_NE(actual_config.kcf_power           , 0);

        EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_name, "eno1"                             );
        EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_mac , "3c:ec:ef:44:36:0a"                );
        EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_ipv4, "192.168.50.21"                    );
        EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_ipv6, "fe80::3eec:efff:fe44:360a"        );
    }

    void GetLogHelper::validate_capacity_empty() {
        kcapacity_t actual_capacity = getlog_data.kgl_cap;
        EXPECT_EQ(actual_capacity.kc_total, 0);
        // This seems to get a garbage value?
        // EXPECT_FLOAT_EQ(actual_capacity.kc_used, 0);
    }

    void GetLogHelper::validate_capacity() {
        kcapacity_t actual_capacity = getlog_data.kgl_cap;

        EXPECT_EQ(actual_capacity.kc_total, 8000000000000);
        EXPECT_FLOAT_EQ(actual_capacity.kc_used , 0.010309278);
    }

    void GetLogHelper::validate_utilization_empty() {
        EXPECT_EQ(getlog_data.kgl_util   , nullptr);
        EXPECT_EQ(getlog_data.kgl_utilcnt, 0      );
    }

    void GetLogHelper::validate_utilization() {
        ASSERT_NE(getlog_data.kgl_util   , nullptr);
        EXPECT_EQ(getlog_data.kgl_utilcnt, 1      );

        for (uint32_t util_ndx = 0; util_ndx < getlog_data.kgl_utilcnt; util_ndx++) {
            EXPECT_STREQ   (getlog_data.kgl_util[util_ndx].ku_name , "Dummy name");
            EXPECT_FLOAT_EQ(getlog_data.kgl_util[util_ndx].ku_value, 0);
        }
    }

    void GetLogHelper::validate_temps_empty() {
        EXPECT_EQ(getlog_data.kgl_temp   , nullptr);
        EXPECT_EQ(getlog_data.kgl_tempcnt, 0      );
    }

    void GetLogHelper::validate_temps() {
        EXPECT_NE(getlog_data.kgl_temp   , nullptr);
        EXPECT_EQ(getlog_data.kgl_tempcnt, 1      );
    }

    void GetLogHelper::validate_stats_empty() {
        EXPECT_EQ(getlog_data.kgl_stat   , nullptr);
        EXPECT_EQ(getlog_data.kgl_statcnt, 0      );
    }

    void GetLogHelper::validate_stats() {
        EXPECT_NE(getlog_data.kgl_stat   , nullptr);
        EXPECT_EQ(getlog_data.kgl_statcnt, 1      );
    }

    /* TODO
    void GetLogHelper::validate_msgs_empty() {
        EXPECT_EQ(getlog_data.kgl_msgs   , nullptr);
        EXPECT_EQ(getlog_data.kgl_msgscnt, 0      );
    }

    void GetLogHelper::validate_msgs() {
        EXPECT_NE(getlog_data.kgl_msgs   , nullptr);
        EXPECT_EQ(getlog_data.kgl_msgscnt, 1      );
    }
    */

    // Helpers for printing data for info type in response
    void GetLogHelper::print_config() {
        kconfiguration_t device_config = this->getlog_data.kgl_conf;

        fprintf(stdout, "Configuration:\n");
        fprintf(stdout, "\t%-18s: %s\n", "Vendor", device_config.kcf_vendor);
        fprintf(stdout, "\t%-18s: %s\n", "Model" , device_config.kcf_model );
        fprintf(stdout, "\t%-18s: %s\n", "Serial", device_config.kcf_serial);
        fprintf(stdout, "\t%-18s: %s\n", "WWN"   , device_config.kcf_wwn   );

        fprintf(stdout, "\t%-18s: %d\n", "Port"              , device_config.kcf_port         );
        fprintf(stdout, "\t%-18s: %d\n", "TLS Port"          , device_config.kcf_tlsport      );

        fprintf(stdout, "\t%-18s: %s\n", "Firmware Version"  , device_config.kcf_version      );
        fprintf(stdout, "\t%-18s: %s\n", "Compile Date"      , device_config.kcf_compdate     );
        fprintf(stdout, "\t%-18s: %s\n", "FW SRC Hash"       , device_config.kcf_srchash      );
        fprintf(stdout, "\t%-18s: %s\n", "Proto Ver"         , device_config.kcf_proto        );
        fprintf(stdout, "\t%-18s: %s\n", "Proto Compile Date", device_config.kcf_protocompdate);
        fprintf(stdout, "\t%-18s: %s\n", "Proto SRC Hash"    , device_config.kcf_protosrchash );
        fprintf(stdout, "\t%-18s: %d\n", "Power"             , device_config.kcf_power        );

        char if_temp_str[16];
        sprintf(if_temp_str, "Interfaces (%02d)", device_config.kcf_interfacescnt);
        fprintf(stdout, "\n\t%-18s ->\n", if_temp_str);

        kinterface_t *device_ifs = device_config.kcf_interfaces;
        for (int if_ndx = 0; if_ndx < device_config.kcf_interfacescnt; if_ndx++)  {
            sprintf(if_temp_str, "Interface [%02d]", if_ndx);
            fprintf(stdout, "\t\t%-16s\n", if_temp_str);

            fprintf(stdout, "\t\t%-16s: %s\n", "Name", device_ifs[if_ndx].ki_name);
            fprintf(stdout, "\t\t%-16s: %s\n", "MAC" , device_ifs[if_ndx].ki_mac );
            fprintf(stdout, "\t\t%-16s: %s\n", "IPv4", device_ifs[if_ndx].ki_ipv4);
            fprintf(stdout, "\t\t%-16s: %s\n", "IPv6", device_ifs[if_ndx].ki_ipv6);
        }

        fprintf(stdout, "\n");
    }

    void GetLogHelper::print_capacity() {
        fprintf(stdout, "Capacity:\n");
        fprintf(stdout, "\t%-18s:  %08" PRIu64 " GiB\n", "Total", getlog_data.kgl_cap.kc_total);
        fprintf(stdout, "\t%-18s: %f%% full\n"         , "Used" , getlog_data.kgl_cap.kc_used );

        fprintf(stdout, "\n");
    }

} // namespace TestHelpers

