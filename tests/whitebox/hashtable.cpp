#include "hashtable.hpp"

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
            struct kiovec *ptr_empty_kiov = (struct kiovec *) &empty_kiov;

            *entry_data = (kv_t) {
                /*
                .kv_key       = ptr_empty_kiov        ,
                .kv_keycnt    = 1                     ,
                .kv_val       = ptr_empty_kiov        ,
                .kv_valcnt    = 1                     ,
                */
                .kv_key       = nullptr               ,
                .kv_keycnt    = 0                     ,
                .kv_val       = nullptr               ,
                .kv_valcnt    = 0                     ,
                .kv_ver       = nullptr               ,
                .kv_verlen    = 0                     ,
                .kv_newver    = nullptr               ,
                .kv_newverlen = 0                     ,
                .kv_disum     = nullptr               ,
                .kv_disumlen  = 0                     ,
                .kv_ditype    = (kditype_t) 0         ,
                .kv_cpolicy   = (kcachepolicy_t) KC_WB,
            };
        }
    }

    // builder-style setters
    KVEntry* KVEntry::set_key(char *key_name) {
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
    KVEntry* KVEntry::set_val(char *val_data, size_t val_len) {
        void *entry_val = (void *) malloc(sizeof(char) * val_len);

        // return nullptr if malloc failed, or copy key value data into entry_val
        if (entry_val == nullptr) { return nullptr; }
        memcpy(entry_val, val_data, val_len);

        entry_data->kv_val    = ki_keycreate(entry_val, val_len);
        entry_data->kv_valcnt = 1;

        return this;
    }

    KVEntry* KVEntry::with_dbver(Buffer *db_ver) {
        entry_data->kv_verlen = db_ver->len;
        entry_data->kv_ver    = db_ver->data;

        return this;
    }

    KVEntry* KVEntry::with_kvver(Buffer *kv_ver) {
        entry_data->kv_newverlen = kv_ver->len;
        entry_data->kv_newver    = kv_ver->data;

        return this;
    }

    KVEntry* KVEntry::with_disum(Buffer *di_sum) {
        entry_data->kv_disumlen = di_sum->len;
        entry_data->kv_disum    = di_sum->data;

        return this;
    }

    KVEntry* KVEntry::with_ditype(kditype_t sum_type) {
        entry_data->kv_ditype = sum_type;

        return this;
    }

    KVEntry* KVEntry::with_cpolicy(kcachepolicy_t cpolicy_type) {
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
        KVEntry *entry_to_get = new KVEntry();
        entry_to_get->set_key(key_name);

        return this->get_entry(entry_to_get);
    }

    KVEntry* HashTable::del_key(char *key_name) {
        KVEntry *entry_to_del = new KVEntry();
        entry_to_del->set_key(key_name);

        return this->put_entry(entry_to_del);
    }

    KVEntry* HashTable::put_keyval(char *key_name, void *val_data, size_t val_len) {
        KVEntry *new_entry  = new KVEntry();
        
        new_entry->set_key(key_name)
                 ->set_val((char *) val_data, val_len);

        return this->put_entry(new_entry);
    }

    KVEntry* HashTable::get_entry(KVEntry *kv_entry) {
        kv_entry->op_status    = (kstatus_t *) malloc(sizeof(kstatus_t));
        *(kv_entry->op_status) = ki_get(kconn_descriptor, kv_entry->entry_data);

        return kv_entry;
    }

    KVEntry* HashTable::del_entry(KVEntry *kv_entry) {
        kbatch_t *batch_info = nullptr;
        kv_entry->op_status  = (kstatus_t *) malloc(sizeof(kstatus_t));
        
        *(kv_entry->op_status) = ki_del(
            kconn_descriptor    ,
            batch_info          ,
            kv_entry->entry_data
        );

        return kv_entry;
    }

    KVEntry* HashTable::put_entry(KVEntry *kv_entry) {
        kbatch_t *batch_info = nullptr;
        kv_entry->op_status  = (kstatus_t *) malloc(sizeof(kstatus_t));
        
        *(kv_entry->op_status) = ki_put(
            kconn_descriptor    ,
            batch_info          ,
            kv_entry->entry_data
        );

        return kv_entry;
    }

} // namespace TestHelpers
