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
#ifndef __KEYVAL_HELPER_HPP
#define __KEYVAL_HELPER_HPP


#include <stdio.h>
#include <inttypes.h>

#include <gtest/gtest.h>

#include "../kfixtures.hpp"
#include "../hashtable.hpp"

namespace TestHelpers {
    struct KeyValVersion {
        size_t    vector_len;
        uint32_t  process_id;
        uint32_t *vector_clock;

        KeyValVersion(uint32_t, size_t);
        KeyValVersion(uint8_t *ver_buffer);

        Buffer serialize();
    };

    struct TestKVEntry : KVEntry {
        TestKVEntry(kv_t *kv_data, kstatus_t *expected_status
    };

    // For `test_getkey_doesnotexist`
    // char key_str[] = "-ForSureThisisAUniqueKeyName-";

    // For test

    struct TestHashTable : HashTable {
        TestHashTable();

        void getversion_expect(KVEntry *input, KVEntry *expected);
        void    getnext_expect(KVEntry *input, KVEntry *expected);
        void    getprev_expect(KVEntry *input, KVEntry *expected);
        void        get_expect(KVEntry *input, KVEntry *expected);
        void        put_expect(KVEntry *input, KVEntry *expected);
        void        del_expect(KVEntry *input, KVEntry *expected);

        void validate_bytes(void *actual_bytes, size_t actual_len,
                            void *expected_bytes, size_t expected_len,
                            const char *failure_msg);

        void validate_kiovec(struct kiovec *actual, size_t actualcnt,
                             struct kiovec *expected, size_t expectedcnt,
                             const char *failure_msg);

        void validate_keyval(kv_t *actual, kv_t *expected);

        void print_bytes(struct kiovec *keyval_fragments, size_t keyval_fragmentcnt);
        void print_bytes_as_hex(void *, size_t);
        void print_integrity_type(kditype_t);
        void print_cachepolicy(kcachepolicy_t);
        void print_keyval(kv_t *keyval_data);
    };
} // namespace TestHelpers

#endif // __KEYVAL_HELPER_HPP
