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
#ifndef __HASHTABLE_HPP
#define __HASHTABLE_HPP

#include <string.h>

extern "C" {
    #include <kinetic/kinetic.h>
}

namespace TestHelpers {

    struct Buffer {
        size_t  len;
        void   *data;
    };

    struct KVEntry {
        // Member variables
        kstatus_t      *op_status;
        kv_t           *entry_data;

        struct kiovec  *key;
        struct kiovec  *val;

        kditype_t       sum_type;
        kcachepolicy_t  cpolicy_type;

        void           *db_ver, *kv_ver, *di_sum;
        size_t          key_cnt, val_cnt, db_verlen, kv_verlen, di_sumlen;

        // Constructors
        KVEntry();

        // builder-style setters
        KVEntry& set_key(char *key_name);
        KVEntry& set_val(uint8_t *val_data, size_t val_len);

        KVEntry& with_dbver(Buffer *db_ver);
        KVEntry& with_kvver(Buffer *kv_ver);
        KVEntry& with_disum(Buffer *di_sum);

        KVEntry& with_ditype(kditype_t sum_type);
        KVEntry& with_cpolicy(kcachepolicy_t cpolicy_type);

        // convenience functions
        bool  is_success();

    }; // struct KVEntry

    struct HashTable {
        int kconn_descriptor;

        HashTable(int conn_descriptor);

        KVEntry*   get_key(char *key_name);
        kstatus_t* del_key(char *key_name);
        kstatus_t* put_keyval(char *key_name, void *val_data, size_t val_len);

        KVEntry*   get_entry(KVEntry *kv_entry);
        kstatus_t* del_entry(KVEntry *kv_entry);
        kstatus_t* put_entry(KVEntry *kv_entry);

    }; // struct HashTable

} // namespace TestHelpers

#endif // __HASHTABLE_HPP
