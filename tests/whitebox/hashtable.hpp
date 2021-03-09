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
#include <stdlib.h>

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
        kstatus_t  op_status;
        kv_t      *entry_data;

        // Constructors
        KVEntry();
        KVEntry(kv_t *preset_data);

        // builder-style setters
        KVEntry* set_key(char *key_name);
        KVEntry* set_val(char *val_data, size_t val_len);

        KVEntry* with_dbver(Buffer *db_ver);
        KVEntry* with_kvver(Buffer *kv_ver);
        KVEntry* with_disum(Buffer *di_sum);

        KVEntry* with_ditype(kditype_t sum_type);
        KVEntry* with_cpolicy(kcachepolicy_t cpolicy_type);

        // convenience functions
        bool is_success();

    }; // struct KVEntry

    struct HashTable {
        int kconn_descriptor;

        HashTable(int conn_descriptor);

        KVEntry* get_key(char *key_name);
        KVEntry* del_key(char *key_name);
        KVEntry* put_keyval(char *key_name, void *val_data, size_t val_len);

        KVEntry* getversion_entry(KVEntry *kv_entry);
        KVEntry* getnext_entry(KVEntry *kv_entry);
        KVEntry* getprev_entry(KVEntry *kv_entry);
        KVEntry* get_entry(KVEntry *kv_entry);
        KVEntry* del_entry(KVEntry *kv_entry);
        KVEntry* put_entry(KVEntry *kv_entry);

    }; // struct HashTable

} // namespace TestHelpers

#endif // __HASHTABLE_HPP
