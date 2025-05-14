#include <stdio.h>
#include <stdlib.h>
#include <string.h> // For strlen, memcpy
#include <assert.h>
#include <unistd.h>

#include "rocksdb/c.h" // Main RocksDB C API header

#include "../include/kv_store.h"
// #include "../include/globals.h" // Only if MAX_KEY_LEN etc. are needed for assertions
// #include "../include/log_file.h" // Only if logfile_t is actively used by these functions now
// #include "../include/xxhash.h" // Not needed anymore

kv_store_t kv_store_init(const char* db_path, logfile_t* log_file) {
    kv_store_t map;
    map.db = NULL;
    map.options = NULL;
    map.write_options = NULL;
    map.read_options = NULL;
    map.logfile = log_file; // Store if still needed for other app logic

    map.options = rocksdb_options_create();
    if (!map.options) {
        fprintf(stderr, "kv_store_init: failed to create rocksdb_options_t\n");
        // Potentially return a kv_store_t indicating an error state or handle error differently
        return map; // Returning a partially uninitialized map is not ideal,
                    // consider a different error reporting mechanism for init
    }
    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    rocksdb_options_increase_parallelism(map.options, (int)(cpus));
    rocksdb_options_optimize_level_style_compaction(map.options, 0);
    rocksdb_options_set_create_if_missing(map.options, 1);

    char* err = NULL;
    map.db = rocksdb_open(map.options, db_path, &err);

    if (err != NULL) {
        fprintf(stderr, "kv_store_init: rocksdb_open failed: %s\n", err);
        free(err);
        rocksdb_options_destroy(map.options);
        map.options = NULL;
        map.db = NULL; // Ensure db is NULL on error
        return map;   // Same as above, error handling for init could be improved
    }

    map.write_options = rocksdb_writeoptions_create();
    if (!map.write_options) {
        fprintf(stderr, "kv_store_init: failed to create rocksdb_writeoptions_t\n");
        rocksdb_close(map.db);
        rocksdb_options_destroy(map.options);
        map.db = NULL;
        map.options = NULL;
        return map;
    }

    map.read_options = rocksdb_readoptions_create();
    if (!map.read_options) {
        fprintf(stderr, "kv_store_init: failed to create rocksdb_readoptions_t\n");
        rocksdb_writeoptions_destroy(map.write_options);
        rocksdb_close(map.db);
        rocksdb_options_destroy(map.options);
        map.write_options = NULL;
        map.db = NULL;
        map.options = NULL;
        return map;
    }

    printf("kv_store_init: RocksDB initialized successfully at %s\n", db_path);
    return map;
}

void kv_store_destroy(kv_store_t* map) {
    if (!map) return;

    if (map->read_options) {
        rocksdb_readoptions_destroy(map->read_options);
        map->read_options = NULL;
    }
    if (map->write_options) {
        rocksdb_writeoptions_destroy(map->write_options);
        map->write_options = NULL;
    }
    if (map->db) {
        rocksdb_close(map->db);
        map->db = NULL;
    }
    if (map->options) { // Options are typically destroyed after the DB is closed
        rocksdb_options_destroy(map->options);
        map->options = NULL;
    }
    printf("kv_store_destroy: RocksDB resources released.\n");
}

uint8_t* kv_store_get(const kv_store_t* map, const char* key, int64_t* value_length) {
    assert(map != NULL && map->db != NULL && key != NULL && value_length != NULL);
    // assert(strlen(key) <= MAX_KEY_LEN); // If you still have MAX_KEY_LEN

    char* err = NULL;
    size_t rocks_value_len;
    char* rocks_value = rocksdb_get(
        map->db,
        map->read_options,
        key, strlen(key),
        &rocks_value_len,
        &err
    );

    if (err != NULL) {
        fprintf(stderr, "kv_store_get: rocksdb_get failed for key '%s': %s\n", key, err);
        free(err);
        *value_length = -1;
        return NULL;
    }

    if (rocks_value == NULL) {
        // Key not found
        *value_length = -1;
        return NULL;
    }

    // RocksDB returns char*, but our API expects uint8_t*. Direct cast is fine
    // as long as the data is treated as raw bytes. The caller must free this.
    *value_length = (int64_t)rocks_value_len;
    return (uint8_t*)rocks_value;
}

// Note: cmd_byte_offset is removed. If it's data, it must be part of 'value'.
int64_t kv_store_set(kv_store_t* map, const char* key, const uint8_t* value, size_t value_len) {
    assert(map != NULL && map->db != NULL && key != NULL && value != NULL);
    // assert(strlen(key) <= MAX_KEY_LEN);
    // assert(value_len <= MAX_VAL_LEN); // If you still have MAX_VAL_LEN

    char* err = NULL;
    rocksdb_put(
        map->db,
        map->write_options,
        key, strlen(key),
        (const char*)value, value_len, // Cast uint8_t* to const char*
        &err
    );

    if (err != NULL) {
        fprintf(stderr, "kv_store_set: rocksdb_put failed for key '%s': %s\n", key, err);
        free(err);
        return -1; // Error
    }
    return 0; // Success
}

int64_t kv_store_delete(kv_store_t* map, const char* key) {
    assert(map != NULL && map->db != NULL && key != NULL);
    // assert(strlen(key) <= MAX_KEY_LEN);

    char* err = NULL;
    rocksdb_delete(
        map->db,
        map->write_options,
        key, strlen(key),
        &err
    );

    if (err != NULL) {
        fprintf(stderr, "kv_store_delete: rocksdb_delete failed for key '%s': %s\n", key, err);
        free(err);
        return -1; // Error
    }
    return 0; // Success
}

void kv_store_display_values(const kv_store_t* map) {
    assert(map != NULL && map->db != NULL);

    rocksdb_iterator_t* iter = rocksdb_create_iterator(map->db, map->read_options);
    if (!iter) {
        fprintf(stderr, "kv_store_display_values: failed to create iterator\n");
        return;
    }

    printf("--- KV Store Contents (RocksDB) ---\n");
    uint64_t counter = 0;
    rocksdb_iter_seek_to_first(iter);

    while (rocksdb_iter_valid(iter)) {
        counter++;
        size_t key_len;
        const char* key_data = rocksdb_iter_key(iter, &key_len);

        size_t val_len;
        const char* val_data = rocksdb_iter_value(iter, &val_len);

        printf("[%lu] Key: %.*s | Value (first ~20 bytes): ",
               counter,
               (int)key_len, key_data);

        size_t display_val_len = (val_len > 20) ? 20 : val_len;
        for (size_t i = 0; i < display_val_len; i++) {
            // Print as hex or char, depending on expected content
             if (val_data[i] >= 32 && val_data[i] <= 126) { // Printable ASCII
                 printf("%c", val_data[i]);
             } else {
                 printf("\\x%02x", (unsigned char)val_data[i]);
             }
        }
        if (val_len > 20) printf("...");
        printf(" (Total Value Length: %zu)\n", val_len);

        rocksdb_iter_next(iter);
    }

    char* iter_err = NULL;
    rocksdb_iter_get_error(iter, &iter_err);
    if (iter_err != NULL) {
        fprintf(stderr, "kv_store_display_values: iterator error: %s\n", iter_err);
        free(iter_err);
    }

    rocksdb_iter_destroy(iter);
    printf("--- End of KV Store Contents (%lu items) ---\n", counter);
}