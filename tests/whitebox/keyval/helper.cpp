#include "helper.hpp"

namespace TestHelpers {
    // ------------------------------
    // KeyValVersion
    KeyValVersion::KeyValVersion(uint32_t process_id, size_t vector_len) {
        this->vector_len   = vector_len;
        this->process_id   = process_id;

        this->vector_clock = new uint32_t[vector_len];
        memset(this->vector_clock, 0, vector_len);
    }

    KeyValVersion::KeyValVersion(uint8_t *ver_buffer) {
        uint32_t process_id;
        size_t   vector_len;

        memcpy(&process_id, ver_buffer, sizeof(uint32_t));
        memcpy(&vector_len, ver_buffer, sizeof(size_t));

        this->vector_clock = new uint32_t[vector_len];
        memcpy(this->vector_clock, ver_buffer, sizeof(uint32_t) * vector_len);
    }

    struct buffer KeyValVersion::serialize() {
        size_t ver_bytelen    = ((sizeof(uint32_t) * (vector_len + 1)) + sizeof(size_t));
        uint8_t *ver_buffer   = (uint8_t *) malloc(ver_bytelen);
        uint8_t *buffer_alias = ver_buffer;

        memcpy(buffer_alias, &process_id, sizeof(uint32_t));
        buffer_alias += sizeof(uint32_t);

        memcpy(buffer_alias, &vector_len, sizeof(size_t));
        buffer_alias += sizeof(size_t);

        memcpy(buffer_alias, vector_clock, sizeof(uint32_t) * vector_len);

        return (struct buffer) { .len = ver_bytelen, .data = ver_buffer };
    }


    // ------------------------------
    // KeyValHelper
    KeyValHelper::KeyValHelper() {}

    void KeyValHelper::test_getversion(int conn_descriptor, kstatus_t *exp_status,
                                       kv_t *actual, kv_t *expected) {
        kstatus_t get_cmd_status = ki_getversion(conn_descriptor, actual);
        // getkey_helper->print_keyval();

        ASSERT_EQ(get_cmd_status.ks_code, exp_status->ks_code);
        KFixtures::validate_status(
            &get_cmd_status,
            exp_status->ks_code, exp_status->ks_message, exp_status->ks_detail
        );

        this->validate_keyval(actual, expected);
    }

    void KeyValHelper::test_getnext(int conn_descriptor, kstatus_t *exp_status,
                                    kv_t *actual, kv_t *actual_next,
                                    kv_t *expected, kv_t *expected_next) {
        kstatus_t get_cmd_status = ki_getnext(conn_descriptor, actual, actual_next);
        // getkey_helper->print_keyval();

        ASSERT_EQ(get_cmd_status.ks_code, exp_status->ks_code);
        KFixtures::validate_status(
            &get_cmd_status,
            exp_status->ks_code, exp_status->ks_message, exp_status->ks_detail
        );

        this->validate_keyval(actual, expected);
        this->validate_keyval(actual_next, expected_next);
    }

    void KeyValHelper::test_getprev(int conn_descriptor, kstatus_t *exp_status,
                                    kv_t *actual, kv_t *actual_prev,
                                    kv_t *expected, kv_t *expected_prev) {
        kstatus_t get_cmd_status = ki_getprev(conn_descriptor, actual, actual_prev);
        // getkey_helper->print_keyval();

        ASSERT_EQ(get_cmd_status.ks_code, exp_status->ks_code);
        KFixtures::validate_status(
            &get_cmd_status,
            exp_status->ks_code, exp_status->ks_message, exp_status->ks_detail
        );

        this->validate_keyval(actual, expected);
        this->validate_keyval(actual_prev, expected_prev);
    }

    void KeyValHelper::test_getkey(int conn_descriptor, kstatus_t *exp_status,
                                   kv_t *actual, kv_t *expected) {
        kstatus_t get_cmd_status = ki_get(conn_descriptor, actual);
        // getkey_helper->print_keyval();

        ASSERT_EQ(get_cmd_status.ks_code, exp_status->ks_code);
        KFixtures::validate_status(
            &get_cmd_status,
            exp_status->ks_code, exp_status->ks_message, exp_status->ks_detail
        );

        this->validate_keyval(actual, expected);
    }

    void KeyValHelper::test_putkey(int conn_descriptor, kstatus_t *exp_status,
                                   kv_t *actual, kv_t *expected) {
        kbatch_t *batch_info     = NULL;
        kstatus_t put_cmd_status = ki_put(conn_descriptor, batch_info, actual);
        // putkey_helper->print_keyval();

        ASSERT_EQ(put_cmd_status.ks_code, exp_status->ks_code);
        KFixtures::validate_status(
            &put_cmd_status,
            exp_status->ks_code, exp_status->ks_message, exp_status->ks_detail
        );

        this->validate_keyval(actual, expected);
    }

    void KeyValHelper::test_delkey(int conn_descriptor, kstatus_t *exp_status,
                                   kv_t *actual, kv_t *expected) {
        kbatch_t *batch_info = nullptr;
        kstatus_t del_cmd_status = ki_cad(conn_descriptor, batch_info, actual);
        // delkey_helper->print_keyval();

        ASSERT_EQ(del_cmd_status.ks_code, exp_status->ks_code);
        KFixtures::validate_status(
            &del_cmd_status,
            exp_status->ks_code, exp_status->ks_message, exp_status->ks_detail
        );

        this->validate_keyval(actual, expected);
    }

    void KeyValHelper::validate_bytes(void *actual_bytes  , size_t actual_len,
                                      void *expected_bytes, size_t expected_len,
                                      const char *failure_msg) {
        EXPECT_EQ(actual_len, expected_len) << failure_msg;

        for (size_t byte_ndx = 0; byte_ndx < expected_len; byte_ndx++) {
            EXPECT_EQ(
                ((uint8_t *) actual_bytes)[byte_ndx],
                ((uint8_t *) expected_bytes)[byte_ndx]
            ) << failure_msg;
        }
    }

    void KeyValHelper::validate_kiovec(struct kiovec *actual  , size_t actualcnt,
                                       struct kiovec *expected, size_t expectedcnt,
                                       const char *failure_msg) {
        EXPECT_EQ(actualcnt, expectedcnt) << failure_msg;

        if (!actualcnt) {
            EXPECT_EQ(actual, nullptr)   << "(actual) kiov_len is 0 but kiov_base is not null";
        }

        if (!expectedcnt) {
            EXPECT_EQ(expected, nullptr) << "(expected) kiov_len is 0 but kiov_base is not null";
        }

        for (size_t kio_ndx = 0; kio_ndx < expectedcnt; kio_ndx++) {
            validate_bytes(
                actual[kio_ndx].kiov_base  , actual[kio_ndx].kiov_len,
                expected[kio_ndx].kiov_base, expected[kio_ndx].kiov_len,
                failure_msg
            );
        }
    }

    void KeyValHelper::validate_keyval(kv_t *actual, kv_t *expected) {
        // this->print_keyval(actual);

        // validate key and value data
        validate_kiovec(
            actual->kv_key  , actual->kv_keycnt  ,
            expected->kv_key, expected->kv_keycnt,
            "unexpected key string"
        );

        validate_kiovec(
            actual->kv_val  , actual->kv_valcnt  ,
            expected->kv_val, expected->kv_valcnt,
            "unexpected value string"
        );

        // validate old version (dbversion)
        validate_bytes(
            actual->kv_ver  , actual->kv_verlen,
            expected->kv_ver, expected->kv_verlen,
            "unexpected db version"
        );

        // validate new version (newversion)
        validate_bytes(
            actual->kv_newver  , actual->kv_newverlen,
            expected->kv_newver, expected->kv_newverlen,
            "unexpected new (or next) version"
        );

        // validate data integrity checksum
        EXPECT_EQ(actual->kv_ditype, expected->kv_ditype);
        validate_bytes(
            actual->kv_disum  , actual->kv_disumlen,
            expected->kv_disum, expected->kv_disumlen,
            "unexpected checksum"
        );
    }

    void KeyValHelper::print_bytes(struct kiovec *keyval_fragments, size_t keyval_fragmentcnt) {
        if (!keyval_fragments || !keyval_fragmentcnt) { return; }

        // calculate and print the total length
        size_t data_len = 0;
        for (size_t ndx = 0; ndx < keyval_fragmentcnt; ndx++) { data_len += keyval_fragments[ndx].kiov_len; }
        fprintf(stdout, "(%ld): ", data_len);

        // iterate and print each character
        for (size_t fragment_ndx = 0; fragment_ndx < keyval_fragmentcnt; fragment_ndx++) {
            struct kiovec key_fragment = keyval_fragments[fragment_ndx];

            for (size_t byte_ndx = 0; byte_ndx < key_fragment.kiov_len; byte_ndx++) {
                fprintf(stdout, "%c", ((char *) key_fragment.kiov_base)[byte_ndx]);
            }
        }
    }

    void KeyValHelper::print_bytes_as_hex(void *data, size_t datalen) {
        if (data == nullptr or !datalen) { return; }

        uint8_t *data_alias = (uint8_t *) data;
        for (size_t data_ndx = 0; data_ndx < datalen; data_ndx++) {
            fprintf(stdout, "%x ", data_alias[data_ndx]);
        }
    }

    void KeyValHelper::print_integrity_type(kditype_t ditype) {
        switch (ditype) {
            case KDI_INVALID:
                fprintf(stdout, "KDI_INVALID (%d)", ditype);
                break;

            case KDI_SHA1:
                fprintf(stdout, "KDI_SHA1 (%d)", ditype);
                break;

            case KDI_SHA2:
                fprintf(stdout, "KDI_SHA2 (%d)", ditype);
                break;

            case KDI_SHA3:
                fprintf(stdout, "KDI_SHA3 (%d)", ditype);
                break;

            case KDI_CRC32C:
                fprintf(stdout, "KDI_CRC32C (%d)", ditype);
                break;

            case KDI_CRC64:
                fprintf(stdout, "KDI_CRC64 (%d)", ditype);
                break;

            case KDI_CRC32:
                fprintf(stdout, "KDI_CRC32 (%d)", ditype);
                break;

            default:
                fprintf(stdout, "Unknown (%d)", ditype);
                break;
        }
    }

    void KeyValHelper::print_cachepolicy(kcachepolicy_t cpolicy) {
        switch (cpolicy) {
            case KC_INVALID:
                fprintf(stdout, "KC_INVALID (%d)", cpolicy);
                break;

            case KC_WT:
                fprintf(stdout, "KC_WT (%d)", cpolicy);
                break;

            case KC_WB:
                fprintf(stdout, "KC_WB (%d)", cpolicy);
                break;

            case KC_FLUSH:
                fprintf(stdout, "KC_FLUSH (%d)", cpolicy);
                break;

            default:
                fprintf(stdout, "Unknown (%d)", cpolicy);
                break;
        }
    }

    void KeyValHelper::print_keyval(kv_t *keyval_data) {
        fprintf(stdout, "Key Data ");
        print_bytes(keyval_data->kv_key, keyval_data->kv_keycnt);
        fprintf(stdout, "\n");

        fprintf(stdout, "Val Data ");
        print_bytes(keyval_data->kv_val, keyval_data->kv_valcnt);
        fprintf(stdout, "\n");

        fprintf(stdout, "DB Version:\n\t");
        print_bytes_as_hex(keyval_data->kv_ver, keyval_data->kv_verlen);
        fprintf(stdout, "\n");

        fprintf(stdout, "New Version:\n\t");
        print_bytes_as_hex(keyval_data->kv_newver, keyval_data->kv_newverlen);
        fprintf(stdout, "\n");

        fprintf(stdout, "Data integrity type: ");
        print_integrity_type(keyval_data->kv_ditype);
        fprintf(stdout, "\n");

        fprintf(stdout, "Cache Policy: ");
        print_cachepolicy(keyval_data->kv_cpolicy);
        fprintf(stdout, "\n");
    }

} // namespace TestHelpers
