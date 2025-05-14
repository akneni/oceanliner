#ifndef KV_STORE_H
#define KV_STORE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Forward declaration for RocksDB types from rocksdb/c.h
// This avoids directly including "rocksdb/c.h" in this public header,
// which is good practice if users of kv_store.h don't all need to know about RocksDB internals.
// However, for simplicity in this example, we might include it directly later in the .c file.
struct rocksdb_t;
struct rocksdb_options_t;
struct rocksdb_writeoptions_t;
struct rocksdb_readoptions_t;

// If logfile_t is still needed for other parts of your application, keep its definition.
// For the KV store itself, RocksDB handles logging.
// For this example, I'm assuming logfile_t might be used elsewhere or its role changes.
#include "../include/globals.h" // If MAX_KEY_LEN, MAX_VAL_LEN are still relevant
#include "../include/log_file.h" // If logfile_t is needed by the application using kv_store

typedef struct kv_store_t {
    struct rocksdb_t* db;
    struct rocksdb_options_t* options;
    struct rocksdb_writeoptions_t* write_options;
    struct rocksdb_readoptions_t* read_options;

    // If the logfile is still conceptually linked or used for other app-level logic
    logfile_t* logfile;
} kv_store_t;

/// @brief Initializes the key-value store.
/// @param db_path The filesystem path where RocksDB will store its data.
/// @param log_file Pointer to a logfile structure (its role might change with RocksDB).
/// @return An initialized kv_store_t object.
kv_store_t kv_store_init(const char* db_path, logfile_t* log_file);

/// @brief Destroys the key-value store, closing RocksDB and freeing resources.
/// @param map Pointer to the kv_store_t object.
void kv_store_destroy(kv_store_t* map);

/// @brief Retrieves a value associated with a key.
/// @param map Pointer to the kv_store_t object.
/// @param key The key to lookup.
/// @param value_length Pointer to an int64_t to store the length of the retrieved value.
///                     Set to -1 if the key is not found or an error occurs.
/// @return A pointer to the retrieved value data (must be freed by the caller if not NULL),
///         or NULL if the key is not found or an error occurs.
uint8_t* kv_store_get(const kv_store_t* map, const char* key, int64_t* value_length);

/// @brief Sets a key-value pair in the store.
/// @param map Pointer to the kv_store_t object.
/// @param key The key to set.
/// @param value Pointer to the value data.
/// @param value_len The length of the value data.
/// @return 0 on success, -1 on error.
/// Note: The cmd_byte_offset argument is removed as RocksDB manages its own data layout.
/// If cmd_byte_offset is data that needs to be stored, it must be part of the 'value' itself.
int64_t kv_store_set(kv_store_t* map, const char* key, const uint8_t* value, size_t value_len);

/// @brief Deletes a key-value pair from the store.
/// @param map Pointer to the kv_store_t object.
/// @param key The key to delete.
/// @return 0 on success, -1 on error.
int64_t kv_store_delete(kv_store_t* map, const char* key);

/// @brief Displays values in the store (for debugging).
/// This function will iterate through RocksDB.
/// @param map Pointer to the kv_store_t object.
void kv_store_display_values(const kv_store_t* map);

#endif // KV_STORE_H