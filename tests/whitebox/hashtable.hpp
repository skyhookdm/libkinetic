#ifndef __HASHTABLE_HPP
#define __HASHTABLE_HPP

#include <string.h>
#include <stdlib.h>

extern "C" {
    #include <kinetic/kinetic.h>
}

namespace TestHelpers {

    const struct kiovec empty_kiov = (struct kiovec) {
        .kiov_len  = 0,
        .kiov_base = nullptr,
    };

    struct Buffer {
        size_t  len;
        void   *data;
    };

    struct KVEntry {
        // Member variables
        kstatus_t      *op_status;
        kv_t           *entry_data;

        // Constructors
        KVEntry();

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

        KVEntry* get_entry(KVEntry *kv_entry);
        KVEntry* del_entry(KVEntry *kv_entry);
        KVEntry* put_entry(KVEntry *kv_entry);

    }; // struct HashTable

} // namespace TestHelpers

#endif // __HASHTABLE_HPP
