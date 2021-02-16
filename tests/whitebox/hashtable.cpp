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

namespace TestHelpers {

    /** ------------------------------
     * KVEntry Functions
     **/

    /**
     * Initializes member attributes to "empty" defaults.
     * Use the builder-like setters to set only the desired variables
     */
    KVEntry::KVEntry() {
        entry_data   = (kv_t *) malloc(sizeof(kv_t));

        if (entry_data != nullptr) {
            *entry_data  = (kv_t) {
                .kv_key       = nullptr;
                .kv_keycnt    = 0;
                .kv_val       = nullptr;
                .kv_valcnt    = 0;
                .kv_ver       = nullptr;
                .kv_verlen    = 0;
                .kv_newver    = nullptr;
                .kv_newverlen = 0;
                .kv_disum     = nullptr;
                .kv_disumlen  = 0;
                .kv_ditype    = (kditype_t) 0;
                .kv_cpolicy   = (kcachepolicy_t) KC_WB;
            };
        }
    }

    // builder-style setters
    KVEntry& KVEntry::set_key(char *key_name) {
        size_t key_len  = strlen(key_name);
        char *entry_key = (char *) malloc(sizeof(char) * key_len);

        // return nullptr if malloc failed, or copy key name data into entry_key
        if (entry_key == nullptr) { return nullptr; }
        strncpy(entry_key, key_name, key_len);

        entry_data->kv_key    = ki_keycreate((void *) entry_key, key_len);
        entry_data->kv_keycnt = 1;

        return this;
    }

    /**
     * Creates an iovec of 1 entry from the given value and value length.
     */
    KVEntry& KVEntry::set_val(uint8_t *val_data, size_t val_len) {
        void *entry_val = (void *) malloc(sizeof(uint8_t) * val_len);

        // return nullptr if malloc failed, or copy key value data into entry_val
        if (entry_val == nullptr) { return nullptr; }
        memcpy(entry_val, val_data, val_len);

        entry_data->kv_val    = ki_keycreate(entry_val, val_len);
        entry_data->kv_valcnt = 1;

        return this;
    }

    KVEntry& KVEntry::with_dbver(Buffer *db_ver) {
        entry_data->verlen = db_ver.len;
        entry_data->ver    = db_ver.data;

        return this;
    }

    KVEntry& KVEntry::with_kvver(Buffer *kv_ver) {
        entry_data->newverlen = kv_ver.len;
        entry_data->newver    = kv_ver.data;

        return this;
    }

    KVEntry& KVEntry::with_disum(Buffer *di_sum) {
        entry_data->disumlen = di_sum.len;
        entry_data->disum    = di_sum.data;

        return this;
    }

    KVEntry& KVEntry::with_ditype(kditype_t sum_type) {
        entry_data->kv_ditype = sum_type;
        return this;
    }

    KVEntry& KVEntry::with_cpolicy(kcachepolicy_t cpolicy_type) {
        entry_data->kv_cpolicy = cpolicy_type;
        return this;
    }

    bool KVEntry::is_success() {
        return this->op_status->ks_code == (kstatus_code_t) K_OK;
    }


    /** ------------------------------
     * HashTable Functions
     **/

    HashTable::HashTable(int conn_descriptor) {
        kconn_descriptor = conn_descriptor;
    }

    KVEntry* HashTable::get_key(char *key_name) {
        KVEntry *entry_to_get = new KVEntry().set_key(key_name);

        return this->get_entry(entry_to_get);
    }

    HashTable::del_key(char *key_name) {
        KVEntry *entry_to_del = new KVEntry().set_key(key_name);

        return this->put_entry(entry_to_del);
    }

    kstatus_t* HashTable::put_keyval(char *key_name, void *val_data, size_t val_len) {
        KVEntry *new_entry  = new KVEntry().set_key(key_name)
                                           .set_val(val_data, val_len);

        return this->put_entry(new_entry);
    }

    KVEntry* HashTable::get_entry(KVEntry *kv_entry) {
        return ki_get(kconn_descriptor, kv_entry->entry_data);
    }

    kstatus_t* HashTable::del_entry(KVEntry *kv_entry) {
        kbatch_t *batch_info = nullptr;
        return ki_del(kconn_descriptor, batch_info, kv_entry->entry_data);
    }

    kstatus_t* HashTable::put_entry(KVEntry *kv_entry) {
        kbatch_t *batch_info = nullptr;
        return ki_put(kconn_descriptor, batch_info, kv_entry->entry_data);
    }

} // namespace TestHelpers

#endif // __HASHTABLE_HPP
