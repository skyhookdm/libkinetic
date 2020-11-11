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

    class KeyValTest: public ::testing::Test {
        protected:
            int conn_descriptor;
            TestHelpers::KeyValHelper *keyval_helper;

            KeyValTest() {
                this->conn_descriptor = -1;
                this->keyval_helper   = new TestHelpers::KeyValHelper();
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


    // ------------------------------
    // Read-only Test Cases
    TEST_F(KeyValTest, test_getkey_doesnotexist) {
        // ------------------------------
        // Execute the Test
        char key_str[] = "-ForSureThisisAUniqueKeyName-";
        size_t key_len = strlen(key_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        memcpy(input_key, key_str, key_len);

        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t notfound_status = {
            .ks_code    = (kstatus_code_t) K_ENOTFOUND,
            .ks_message = (char *) "Key not found",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &notfound_status,
                                   &input_data, &output_data);
    }

    TEST_F(KeyValTest, test_getkey_exists) {
        // ------------------------------
        // Execute the Test
        char key_str[] = "pak";
        char val_str[] = "Hello World";
        size_t key_len = strlen(key_str);
        size_t val_len = strlen(val_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        memcpy(input_key, key_str, key_len);

        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        size_t  existing_dbverlen = 10;
        uint8_t existing_dbver[]  = {
            0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30
        };

        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec *output_valvec = ki_keycreate(val_str, val_len);
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = output_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = existing_dbver        ,
            .kv_verlen    = existing_dbverlen     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) KDI_SHA1  ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t ok_status = {
            .ks_code    = (kstatus_code_t) K_OK,
            .ks_message = (char *) "",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &ok_status, &input_data, &output_data);
    }

    // ------------------------------
    // TODO: these tests not yet verified

    TEST_F(KeyValTest, test_getversion_doesnotexist) {
        // ------------------------------
        // Execute the Test
        char key_str[] = "getversion_doesnotexist_38928383";
        size_t key_len = strlen(key_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        memcpy(input_key, key_str, key_len);

        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t notfound_status = {
            .ks_code    = (kstatus_code_t) K_ENOTFOUND,
            .ks_message = (char *) "Key not found",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &notfound_status, &input_data, &output_data);
    }

    TEST_F(KeyValTest, test_getversion_exists) {
        // ------------------------------
        // Execute the Test
        char key_str[] = "getversion_doesnotexist_38928383";
        size_t key_len = strlen(key_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        memcpy(input_key, key_str, key_len);

        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        size_t  existing_dbverlen = 10;
        uint8_t existing_dbver[]  = {
            0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30
        };

        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = existing_dbver        ,
            .kv_verlen    = existing_dbverlen     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t ok_status = {
            .ks_code    = (kstatus_code_t) K_OK,
            .ks_message = (char *) "",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &ok_status, &input_data, &output_data);
    }

    // TODO
    TEST_F(KeyValTest, test_getnext_doesnotexist) {
        // ------------------------------
        // Execute the Test
        char key_str[]     = "UniqueTestKeyThatDoesNotHaveANextKey";
        size_t key_len     = strlen(key_str);
        size_t key_cnt     = 1;
        size_t val_cnt     = 1;

        // TODO
        char *input_key     = (char *) malloc(sizeof(char) * key_len);
        char *input_nextkey = (char *) malloc(sizeof(char) * key_len);

        // Copy from key_str and nextkey_str for inputs, and then use this data for expected
        // outputs
        memcpy(input_key    , key_str, key_len);
        memcpy(input_nextkey, key_str, key_len);

        // Test inputs to be passed to library
        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec input_nextkeyvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        struct kiovec input_nextvalvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_nextdata = (kv_t) {
            .kv_key       = &input_nextkeyvec     ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_nextvalvec     ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        // Expected outputs from the library
        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec *output_nextkeyvec = ki_keycreate(nextkey_str, nextkey_len);
        struct kiovec  output_nextvalvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_nextdata = (kv_t) {
            .kv_key       = output_nextkeyvec     ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_nextvalvec    ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t notfound_status = {
            .ks_code    = (kstatus_code_t) K_ENOTFOUND,
            .ks_message = (char *) "Key not found",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getnext(
            conn_descriptor, &notfound_status,
            &input_data,  &input_nextdata,
            &output_data, &output_nextdata
        );
    }

    TEST_F(KeyValTest, test_getnext_exists) {
        // ------------------------------
        // Execute the Test
        char key_str[]     = "KEY-00088";
        char nextkey_str[] = "KEY-00089";
        size_t key_len     = strlen(key_str);
        size_t nextkey_len = strlen(nextkey_str);
        size_t key_cnt     = 1;
        size_t val_cnt     = 1;

        char *input_key     = (char *) malloc(sizeof(char) * key_len);
        char *input_nextkey = (char *) malloc(sizeof(char) * nextkey_len);

        // Copy from key_str and nextkey_str for inputs, and then use this data for expected
        // outputs
        memcpy(input_key    , key_str    , key_len);
        memcpy(input_nextkey, nextkey_str, nextkey_len);

        // Test inputs to be passed to library
        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec *input_nextkeyvec = ki_keycreate(input_nextkey, nextkey_len);
        struct kiovec  input_nextvalvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_nextdata = (kv_t) {
            .kv_key       = input_nextkeyvec      ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_nextvalvec     ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        // Expected outputs from the library
        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec *output_nextkeyvec = ki_keycreate(nextkey_str, nextkey_len);
        struct kiovec  output_nextvalvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_nextdata = (kv_t) {
            .kv_key       = output_nextkeyvec     ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_nextvalvec    ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t notfound_status = {
            .ks_code    = (kstatus_code_t) K_ENOTFOUND,
            .ks_message = (char *) "Key not found",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getnext(
            conn_descriptor, &notfound_status,
            &input_data,  &input_nextdata,
            &output_data, &output_nextdata
        );
    }

    TEST_F(KeyValTest, test_getprev_doesnotexist) {
        // ------------------------------
        // Execute the Test
        char key_str[] = "getversion_doesnotexist_38928383";
        size_t key_len = strlen(key_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        memcpy(input_key, key_str, key_len);

        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t notfound_status = {
            .ks_code    = (kstatus_code_t) K_ENOTFOUND,
            .ks_message = (char *) "Key not found",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &notfound_status, &input_data, &output_data);
    }

    TEST_F(KeyValTest, test_getprev_exists) {
        // ------------------------------
        // Execute the Test
        char key_str[] = "getversion_doesnotexist_38928383";
        size_t key_len = strlen(key_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        memcpy(input_key, key_str, key_len);

        struct kiovec *input_keyvec = ki_keycreate(input_key, key_len);
        struct kiovec  input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t input_data   = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &input_valvec         ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        size_t  existing_dbverlen = 10;
        uint8_t existing_dbver[]  = {
            0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30
        };

        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec  output_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t output_data  = (kv_t) {
            .kv_key       = output_keyvec         ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = &output_valvec        ,
            .kv_valcnt    = 1                     ,
            .kv_ver       = existing_dbver        ,
            .kv_verlen    = existing_dbverlen     ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kstatus_t ok_status = {
            .ks_code    = (kstatus_code_t) K_OK,
            .ks_message = (char *) "",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &ok_status, &input_data, &output_data);
    }

    // ------------------------------
    // Modification Test Cases
    TEST_F(KeyValTest, test_putkey_getkey_insertcheckdel) {
        // ------------------------------
        // Data Preparation
        char digest_algorithm[] = "sha1";
        char key_str[]          = "A$Unique$Key$Name$";
        char val_str[]          = "Test: Insert-Check-Delete";

        size_t key_len = strlen(key_str);
        size_t val_len = strlen(val_str);
        size_t key_cnt = 1;
        size_t val_cnt = 1;

        char *input_key = (char *) malloc(sizeof(char) * key_len);
        char *input_val = (char *) malloc(sizeof(char) * val_len);

        struct kiovec *input_keyvec, *input_valvec;
        struct kiovec *output_keyvec = ki_keycreate(key_str, key_len);
        struct kiovec *output_valvec = ki_keycreate(val_str, val_len);
        struct kiovec  expected_emptyval;
        memset(&expected_emptyval, 0, sizeof(struct kiovec));

        struct kbuffer keyval_checksum = compute_digest(output_valvec, val_cnt, digest_algorithm);
        ASSERT_NE(keyval_checksum.base, nullptr);

        TestHelpers::KeyValVersion *ver = new TestHelpers::KeyValVersion(101, 3);
        ver->vector_clock[0] += 1;

        TestHelpers::buffer ver_buffer = ver->serialize();

        kstatus_t ok_status = {
            .ks_code    = (kstatus_code_t) K_OK,
            .ks_message = (char *) "",
            .ks_detail  = nullptr,
        };

        // ------------------------------
        // First: insert the key and validate
        fprintf(stdout, "Step 1: Put key\n");

        memcpy(input_key, key_str, key_len);
        memcpy(input_val, val_str, val_len);
        input_keyvec = ki_keycreate(input_key, key_len);
        input_valvec = ki_keycreate(input_val, val_len);
        kv_t putkey_input = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = input_valvec          ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = nullptr               ,
            .kv_verlen    = 0                     ,
            .kv_newver    = ver_buffer.data       ,
            .kv_newverlen = ver_buffer.len        ,
            .kv_disum     = keyval_checksum.base  ,
            .kv_disumlen  = keyval_checksum.len   ,
            .kv_ditype    = (kditype_t) KDI_SHA1  ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kv_t putkey_output = (kv_t) {
            .kv_key        = output_keyvec         ,
            .kv_keycnt     = key_cnt               ,
            .kv_val        = output_valvec         ,
            .kv_valcnt     = val_cnt               ,
            .kv_ver        = nullptr               ,
            .kv_verlen     = 0                     ,
            .kv_newver     = ver_buffer.data       ,
            .kv_newverlen  = ver_buffer.len        ,
            .kv_disum      = keyval_checksum.base  ,
            .kv_disumlen   = keyval_checksum.len   ,
            .kv_ditype     = (kditype_t) KDI_SHA1  ,
            .kv_cpolicy    = (kcachepolicy_t) KC_WB,
        };

        keyval_helper->test_putkey(conn_descriptor, &ok_status,
                                   &putkey_input, &putkey_output);

        // ------------------------------
        // Second: get the key and validate
        fprintf(stdout, "Step 2: Get key\n");

        memcpy(input_key, key_str, key_len);
        input_keyvec  = ki_keycreate(input_key, key_len);
        *input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t getkey_input1 = (kv_t) {
            .kv_key        = input_keyvec          ,
            .kv_keycnt     = key_cnt               ,
            .kv_val        = input_valvec          ,
            .kv_valcnt     = 1                     ,
            .kv_ver        = nullptr               ,
            .kv_verlen     = 0                     ,
            .kv_newver     = nullptr               ,
            .kv_newverlen  = 0                     ,
            .kv_disum      = nullptr               ,
            .kv_disumlen   = 0                     ,
            .kv_ditype     = (kditype_t) 0         ,
            .kv_cpolicy    = (kcachepolicy_t) KC_WB,
        };

        kv_t getkey_output1 = (kv_t) {
            .kv_key         = output_keyvec         ,
            .kv_keycnt      = key_cnt               ,
            .kv_val         = output_valvec         ,
            .kv_valcnt      = val_cnt               ,
            .kv_ver         = ver_buffer.data       ,
            .kv_verlen      = ver_buffer.len        ,
            .kv_newver      = nullptr               ,
            .kv_newverlen   = 0                     ,
            .kv_disum       = keyval_checksum.base  ,
            .kv_disumlen    = keyval_checksum.len   ,
            .kv_ditype      = (kditype_t) KDI_SHA1  ,
            .kv_cpolicy     = (kcachepolicy_t) KC_WB,
        };

        keyval_helper->test_getkey(conn_descriptor, &ok_status,
                                   &getkey_input1, &getkey_output1);

        // ------------------------------
        // Third: delete the key and validate
        fprintf(stdout, "Step 3: Del key\n");

        memcpy(input_key, key_str, key_len);
        input_keyvec  = ki_keycreate(input_key, key_len);
        *input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t delkey_input = (kv_t) {
            .kv_key       = input_keyvec          ,
            .kv_keycnt    = key_cnt               ,
            .kv_val       = input_valvec          ,
            .kv_valcnt    = val_cnt               ,
            .kv_ver       = ver_buffer.data       ,
            .kv_verlen    = ver_buffer.len        ,
            .kv_newver    = nullptr               ,
            .kv_newverlen = 0                     ,
            .kv_disum     = nullptr               ,
            .kv_disumlen  = 0                     ,
            .kv_ditype    = (kditype_t) 0         ,
            .kv_cpolicy   = (kcachepolicy_t) KC_WB,
        };

        kv_t delkey_output = (kv_t) {
            .kv_key        = output_keyvec         ,
            .kv_keycnt     = 1                     ,
            .kv_val        = &expected_emptyval    ,
            .kv_valcnt     = 1                     ,
            .kv_ver        = ver_buffer.data       ,
            .kv_verlen     = ver_buffer.len        ,
            .kv_newver     = nullptr               ,
            .kv_newverlen  = 0                     ,
            .kv_disum      = nullptr               ,
            .kv_disumlen   = 0                     ,
            .kv_ditype     = (kditype_t) 0         ,
            .kv_cpolicy    = (kcachepolicy_t) KC_WB,
        };

        keyval_helper->test_delkey(conn_descriptor, &ok_status,
                                   &delkey_input, &delkey_output);

        fprintf(stdout, "Delete key input:\n");
        keyval_helper->print_keyval(&delkey_input);

        fprintf(stdout, "Delete key output:\n");
        keyval_helper->print_keyval(&delkey_output);

        // ------------------------------
        // Fourth: validate the key does not exist
        fprintf(stdout, "Step 4: Get key (no longer exists)\n");

        memcpy(input_key, key_str, key_len);
        input_keyvec  = ki_keycreate(input_key, key_len);
        *input_valvec = (struct kiovec) { .kiov_len = 0, .kiov_base = nullptr };
        kv_t getkey_input2 = (kv_t) {
            .kv_key        = input_keyvec          ,
            .kv_keycnt     = key_cnt               ,
            .kv_val        = input_valvec          ,
            .kv_valcnt     = 1                     ,
            .kv_ver        = nullptr               ,
            .kv_verlen     = 0                     ,
            .kv_newver     = nullptr               ,
            .kv_newverlen  = 0                     ,
            .kv_disum      = nullptr               ,
            .kv_disumlen   = 0                     ,
            .kv_ditype     = (kditype_t) 0         ,
            .kv_cpolicy    = (kcachepolicy_t) KC_WB,
        };

        kv_t getkey_output2 = (kv_t) {
            .kv_key         = output_keyvec         ,
            .kv_keycnt      = key_cnt               ,
            .kv_val         = &expected_emptyval    ,
            .kv_valcnt      = 1                     ,
            .kv_ver         = nullptr               ,
            .kv_verlen      = 0                     ,
            .kv_newver      = nullptr               ,
            .kv_newverlen   = 0                     ,
            .kv_disum       = nullptr               ,
            .kv_disumlen    = 0                     ,
            .kv_ditype      = (kditype_t) 0         ,
            .kv_cpolicy     = (kcachepolicy_t) KC_WB,
        };

        kstatus_t notfound_status = {
            .ks_code    = (kstatus_code_t) K_ENOTFOUND,
            .ks_message = (char *) "Key not found",
            .ks_detail  = nullptr,
        };

        keyval_helper->test_getkey(conn_descriptor, &notfound_status,
                                   &getkey_input2, &getkey_output2);

        // ------------------------------
        // cleanup
        free(keyval_checksum.base);

        putkey_input.destroy_protobuf(&putkey_input);
        getkey_input1.destroy_protobuf(&getkey_input1);
        delkey_input.destroy_protobuf(&delkey_input);

        // Can't call destroy if the result was a failure, this probably warrants changes in
        // the library
        // regetkey_helper->keyval_data.destroy_protobuf(&(regetkey_helper->keyval_data));
    }

} // namespace KeyVal


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
