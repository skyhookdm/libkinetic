#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <gtest/gtest.h>

extern "C" {
    #include <kinetic/kinetic.h>
}


struct test_result {
    int success_count;
    int error_count;
};

struct context {
    const char    *host;        // connection host
    const char    *port;        // connection port, ex "8123"
    const char    *hkey;        // connection hmac key
    int64_t        user;        // connection user ID
    uint32_t       usetls;      // connection boolean to use TLS (8443)
    int64_t        clustervers; // Client cluster version number
    klimits_t      limits;      // Kinetic server limits
};

const char test_host[] = "127.0.0.1";
const char test_port[] = "8123";
const char test_hkey[] = "asdfasdf";

struct context test_context = {
    .host        = test_host,
    .port        = test_port,
    .hkey        = test_hkey,

    .user        =  1,
    .usetls      =  0,
    .clustervers = -1,
};

void check_status(kstatus_t *command_status, kstatus_t *expected_status) {
    ASSERT_EQ(command_status->ks_code, expected_status->ks_code);

    ASSERT_STREQ(command_status->ks_message, expected_status->ks_message);
    ASSERT_STREQ(command_status->ks_detail , expected_status->ks_detail );
}

class GetLogHelper {
    public:
        kgetlog_t getlog_data;
        kgltype_t getlog_infotypes[10];

        GetLogHelper() {
            memset((void *) &(this->getlog_data), 0, sizeof(kgetlog_t));
            this->getlog_data.kgl_type = getlog_infotypes;
        }

        // Helpers for setting info types in request
        void add_config() {
            this->getlog_data.kgl_type[this->getlog_data.kgl_typecnt++] = (kgltype_t) KGLT_CONFIGURATION;
        }

        void add_capacity() {
            this->getlog_data.kgl_type[this->getlog_data.kgl_typecnt++] = (kgltype_t) KGLT_CAPACITIES;
        }

        // Helpers for printing data for info type in response
        void print_config() {
            kconfiguration_t device_config = this->getlog_data.kgl_conf;

            fprintf(stdout, "Configuration:\n");
            fprintf(stdout, "\t%-18s: %s\n", "Vendor", device_config.kcf_vendor);
            fprintf(stdout, "\t%-18s: %s\n", "Model" , device_config.kcf_model );
            fprintf(stdout, "\t%-18s: %s\n", "Serial", device_config.kcf_serial);
            fprintf(stdout, "\t%-18s: %s\n", "WWN"   , device_config.kcf_wwn   );

            fprintf(stdout, "\t%-18s: %d\n", "Port"              , device_config.kcf_port         );
            fprintf(stdout, "\t%-18s: %d\n", "TLS Port"          , device_config.kcf_tlsport      );

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

        void print_capacity() {
            fprintf(stdout, "Capacity:\n");
            fprintf(stdout, "\t%-18s:  %08" PRIu64 "GiB\n", "Total", this->getlog_data.kgl_cap.kc_total);
            fprintf(stdout, "\t%-18s: %f%% full\n"        , "Used" , this->getlog_data.kgl_cap.kc_used );

            fprintf(stdout, "\n");
        }
};

class GetLogTest: public ::testing::Test {
    protected:
        int          conn_descriptor;
        GetLogHelper *getlog_helper;

        GetLogTest() {
            this->conn_descriptor = -1;
            this->getlog_helper   = new GetLogHelper();
        }

        void SetUp() override {
            this->conn_descriptor = ki_open(
                (char *) test_context.host,
                (char *) test_context.port,
                test_context.usetls,
                test_context.user,
                (char *) test_context.hkey
            );

            if (this->conn_descriptor < 0) {
                fprintf(stderr, "Test Connection Failed\n");
                return;
            }

            test_context.limits = ki_limits(this->conn_descriptor);
            if (!test_context.limits.kl_keylen) {
                fprintf(stderr, "Failed to receive kinetic device limits\n");
            }
        }

        void TearDown() override {
            if (this->conn_descriptor >= 0) {
                ki_close(this->conn_descriptor);
            }
        }
};

TEST_F(GetLogTest, test_config_infotype_1) {
    // ------------------------------
    // Execute test
    getlog_helper->add_config();
    kstatus_t cmd_status = ki_getlog(conn_descriptor, &(getlog_helper->getlog_data));

    // ------------------------------
    // Verify response data against test expectations

    // verify returned status
    EXPECT_EQ   (cmd_status.ks_code   , (kstatus_code_t) K_OK);
    EXPECT_STREQ(cmd_status.ks_message, (char *) ""          );
    EXPECT_EQ   (cmd_status.ks_detail , nullptr              );

    // verify returned front-end getlog struct
    kgetlog_t actual_getlog = getlog_helper->getlog_data;

    EXPECT_NE(actual_getlog.kgl_protobuf, nullptr);
    EXPECT_NE(actual_getlog.kgl_type    , nullptr);
    EXPECT_EQ(actual_getlog.kgl_util    , nullptr);
    EXPECT_EQ(actual_getlog.kgl_temp    , nullptr);
    EXPECT_EQ(actual_getlog.kgl_stat    , nullptr);
    // EXPECT_EQ(actual_getlog.kgl_msgs    , nullptr);

    EXPECT_EQ(actual_getlog.kgl_typecnt , 1                             );
    EXPECT_EQ(actual_getlog.kgl_type[0] , (kgltype_t) KGLT_CONFIGURATION);

    kconfiguration_t actual_config = actual_getlog.kgl_conf;
    EXPECT_STREQ(actual_config.kcf_vendor, "Seagate"            );
    EXPECT_STREQ(actual_config.kcf_model , "Drive Model"        );
    EXPECT_STREQ(actual_config.kcf_serial, "Drive SN"           );
    EXPECT_STREQ(actual_config.kcf_wwn   , "Drive WWN"          );

	// ?
    // EXPECT_STREQ(actual_config.kcf_version , "4.0.1"        ); 
    EXPECT_STREQ(actual_config.kcf_compdate     , "Tue Mar  3 14:27:35 PST 2020"        );
	EXPECT_STREQ(actual_config.kcf_srchash      , ""                                    );
	EXPECT_STREQ(actual_config.kcf_proto        , "4.0.1"                               );
    EXPECT_STREQ(actual_config.kcf_protocompdate, "Tue Mar  3 14:27:35 PST 2020"        );
	EXPECT_STREQ(actual_config.kcf_protosrchash , "f8d5d18"                             );

	EXPECT_EQ(actual_config.kcf_port            , 8123                        );
	EXPECT_EQ(actual_config.kcf_tlsport         , 8443                        );
	// EXPECT_EQ(actual_config.kcf_power           , (kpltype_t) KPLT_OPERATIONAL);
	EXPECT_EQ(actual_config.kcf_power           , 0 );
	EXPECT_EQ(actual_config.kcf_interfacescnt   , 1                           );

	EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_name, "eno1"                             );
	EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_mac , "3c:ec:ef:44:36:0a"                );
	EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_ipv4, "192.168.50.21"                    );
	EXPECT_STREQ(actual_config.kcf_interfaces[0].ki_ipv6, "fe80::3eec:efff:fe44:360a"        );

    // Temporary print during test development
    getlog_helper->print_config();
}

TEST_F(GetLogTest, test_capacity_infotype_1) {
    // set test expectations
    kstatus_t expected_status = (kstatus_t) {
        .ks_code    = (kstatus_code_t) K_OK,
        .ks_message = (char *) ""          ,
        .ks_detail  = NULL                 ,
    };

    getlog_helper->add_capacity();

    kstatus_t cmd_status = ki_getlog(conn_descriptor, &(getlog_helper->getlog_data));
    EXPECT_EQ   (cmd_status.ks_code   , expected_status.ks_code   );
    EXPECT_STREQ(cmd_status.ks_message, expected_status.ks_message);
    EXPECT_EQ   (cmd_status.ks_detail , expected_status.ks_detail );

    getlog_helper->print_capacity();
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
