#include "kfixtures.hpp"

extern char *ki_status_label[];

namespace KFixtures {

    const char test_host[] = "127.0.0.1";
    const char test_port[] = "8123";
    const char test_hkey[] = "asdfasdf";

    void
    validate_status(kstatus_t *cmd_status,
                    kstatus_code_t exp_code,
                    char *exp_msg,
                    char *exp_msgdetail) {

        EXPECT_EQ(cmd_status->ks_code, exp_code);

        // if the expected message is null, use a different expect function
        if (exp_msg == nullptr) { EXPECT_EQ   (cmd_status->ks_message, nullptr ); }
        else                    { EXPECT_STREQ(cmd_status->ks_message, exp_msg ); }

        // if the expected message detail is null, use a different expect function
        if (exp_msgdetail == nullptr) { EXPECT_EQ   (cmd_status->ks_detail, nullptr      ); }
        else                          { EXPECT_STREQ(cmd_status->ks_detail, exp_msgdetail); }
    }

    void print_status(kstatus_t *cmd_status) {
        fprintf(stdout,
            "Status Code (%d): %s\n",
            cmd_status->ks_code,
            ki_status_label[cmd_status->ks_code]
        );

        if (cmd_status->ks_message) {
            fprintf(stdout, "Status Message: %s\n", cmd_status->ks_message);
        }

        if (cmd_status->ks_detail) {
            fprintf(stdout, "Status Detail: %s\n", cmd_status->ks_detail);
        }
    }
}
