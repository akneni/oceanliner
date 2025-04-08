# Ocean Liner
- Ocean Liner provides a raft-replicated key value store. More specifically, the key value store will be `HashMap<String, Bytes>`; both the string and the bytes will be arbitrarily sized.  

## RSM State
- The state that is maintained by an append only log file. 
- In our implementation, we will maintain two append only log files, one for metadata and another for the commands. 
- The metadata log file will store the following fixed length data. 
```c
// 24 bytes
struct {
    uint32_t term;
    uint32_t num_commands; // The number of commands stored at this log index (this allows us to batch appendEntries calls)
    uint64_t log_index;
    uint64_t byte_offset;  // The byte offset of the first command in the command log file
}
```

- The command log file will store the following entries. 
```c
// Arbitrary length
struct {
    uint8_t command;        // (enum with GET, SET, DELETE, etc.)
    char key[...];          // Null terminated string
    uint64_t value_length;  // length of the value (note, the key string needs to be padded so that this field can be 8 byte aligned).
    uint8_t value[...];
}
```

## Key Value Structure
- In addition to the append only log file, we also need to actually maintain a hashmap in order to service get commands without iterating though the entire log file.
- In order to maintain a HashMap on disk, we will need to make each entry fixed length. 
- A hashmap entry be stored as is shown below. The first 17 characters of the string will be stored inline (leaving space for the null terminator character). The `cmd_byte_offset` is the byte offset of the relevant SET command in the commands log file. We can get the fill key and value by following this byte offset.
```c
// 32 bytes
struct {
    uint64_t cmd_byte_offset;
    uint32_t key_length;
    char string[18];
    uint8_t inline_value_slot;  // UINT8_MAX if value is not inlined
    uint8_t inline_value_len;
}
```

- The hashmap file will also store the following metadata. 
```c
struct {
    uint64_t last_committed_index;
    uint64_t last_committed_term;
    uint64_t num_slots;
    uint64_t num_entries;
}
```
- We will also maintain an in-memory bloom filter to possibly skip disk reads.

- **Possible Inline Value store optimization** => We group each slot into a page (4096 bytes).
    - bytes 0 - 3200 => Store 100 KV slots (some of these slots may be unused in the last page to allow for power-of-2 sizing)
    - bytes 3200 - 3264 => Store packed bitmap of which inline value slots are taken. (we're only using 13 of the 64 bits here, maybe we can find a use for the remaining 51 bits?)
    - bytes 3264 - 4096 => Stores 13 slots for inline values stores that are 64 bytes each. 
