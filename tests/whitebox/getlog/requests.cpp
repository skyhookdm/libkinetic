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

#include "../kfixtures.hpp"
#include "helper.hpp"


namespace KFixtures {

    void check_status(kstatus_t *command_status, kstatus_t *expected_status) {
        ASSERT_EQ(command_status->ks_code, expected_status->ks_code);

        ASSERT_STREQ(command_status->ks_message, expected_status->ks_message);
        ASSERT_STREQ(command_status->ks_detail , expected_status->ks_detail );
    }

    class GetLogTest: public ::testing::Test {
        protected:
            int conn_descriptor;
            TestHelpers::GetLogHelper *getlog_helper;

            GetLogTest() {
                this->conn_descriptor = -1;
                this->getlog_helper   = new TestHelpers::GetLogHelper();
            }

            void SetUp() override {
                this->conn_descriptor = ki_open(
                    (char *) KFixtures::test_host,
                    (char *) KFixtures::test_port,
                    0, // usetls
                    1, // user ID
                    (char *) KFixtures::test_hkey
                );

                if (this->conn_descriptor < 0) {
                    fprintf(stderr, "Test Connection Failed\n");
                    return;
                }

                klimits_t device_limits = ki_limits(this->conn_descriptor);
                if (!device_limits.kl_keylen) {
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
        KFixtures::validate_status(&cmd_status,
            (kstatus_code_t) K_OK, (char *) "", nullptr
        );

        // verify returned front-end getlog struct
        kgetlog_t actual_getlog = getlog_helper->getlog_data;


        EXPECT_NE(actual_getlog.kgl_protobuf, nullptr);
        EXPECT_NE(actual_getlog.kgl_type    , nullptr);
        EXPECT_EQ(actual_getlog.kgl_typecnt , 1      );
        EXPECT_EQ(actual_getlog.kgl_type[0] , (kgltype_t) KGLT_CONFIGURATION);

        getlog_helper->validate_config();

        getlog_helper->validate_capacity_empty();
        getlog_helper->validate_utilization_empty();
        getlog_helper->validate_temps_empty();
        getlog_helper->validate_stats_empty();
        // getlog_helper->validate_msgs_empty();

        // Temporary print during test development
        // getlog_helper->print_config();
    }

    TEST_F(GetLogTest, test_capacity_infotype_1) {
        // ------------------------------
        // Execute test
        getlog_helper->add_capacity();
        kstatus_t cmd_status = ki_getlog(conn_descriptor, &(getlog_helper->getlog_data));

        // ------------------------------
        // Verify response data against test expectations

        // verify returned status
        KFixtures::validate_status(&cmd_status,
            (kstatus_code_t) K_OK, (char *) "", nullptr
        );

        // verify returned front-end getlog struct
        kgetlog_t actual_getlog = getlog_helper->getlog_data;

        EXPECT_NE(actual_getlog.kgl_protobuf, nullptr);
        EXPECT_NE(actual_getlog.kgl_type    , nullptr);
        EXPECT_EQ(actual_getlog.kgl_typecnt , 1      );
        EXPECT_EQ(actual_getlog.kgl_type[0] , (kgltype_t) KGLT_CAPACITIES);

        // getlog_helper->validate_capacity();

        getlog_helper->validate_config_empty();
        getlog_helper->validate_utilization_empty();
        getlog_helper->validate_temps_empty();
        getlog_helper->validate_stats_empty();
        // getlog_helper->validate_msgs_empty();

        // Temporary print during test development
        // getlog_helper->print_capacity();
    }

} // namespace GetLog


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
