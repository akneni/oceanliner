# Ocean Liner
- Ocean Liner provides a raft-replicated key value store. More specifically, the key value store will be `HashMap<String, Bytes>`; both the string and the bytes will be arbitrarily sized.  

## RSM State
- The state that is maintained by an append only log file. 
```c
// 64 bytes
struct LogEntryHeader {
    uint32_t term;
    uint32_t num_commands; // The number of commands stored at this log index (this allows us to batch appendEntries calls)
    uint64_t log_index;
    uint64_t command_length;  // The Length of all the commands in bytes. 
    uint8_t header_hash[24];  // Hash of the header. If the hashes don't line up, then we assume it was because of a partial write. 
    uint8_t magic_number[16];    // A random constant that we use to ensure that the field in question is a header.
}

// header1 and header2 are copies of each other and allow us to iterate over our log file in forward or reverse order. 
// (iterating in reverse order may be necessary when a newly appointed leader overwrites uncommitted entries)
struct {
    LogEntryHeader header1;
    uint8_t commands[...];
    LogEntryHeader header2;
}
```
- A challenge with constructing this is to handle partial writes. One solution is to limit a log to be <= 4KB to ensure that an atomic write can't occur at a hardware level, but this would limit the batch size. Alternatively, we use hashes of the header and a magic number to ensure that a 64 byte length of data is a valid header. Even both of these techniques together are not mathematically guaranteed root out all false positives, but the change of a false positive is astronomically low. 
- In the event of a torn write, we can iterate backwards 


- The command log file will store the following entries. 
```c
// Arbitrary length
struct {
    uint8_t command;        // (enum with SET, DELETE, etc.) (GET not included as it doesn't change state)
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
    uint32_t key_length;        // Is this field is 0, then this slot is empty
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

## Out Of Scope
- Secure communication. All networking will happen over TCP/UDP (without authentication or encryption) in order to allow us to spend more time on throughput optimization. 
- Log Compaction. This is necessary for long running systems, but not for this project. 
- Handling disk corruption. This is also necessary for production systems, but is not extremely pressing for our proof of concept (especially as many disk controllers already implement checksum validation to some extent). 