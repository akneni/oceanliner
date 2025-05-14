# Ocean Liner
- Ocean Liner provides a raft-replicated key value store. More specifically, the key value store will be `HashMap<String, Bytes>`; both the string and the bytes will be arbitrarily sized.  

## Out Of Scope
- Secure communication. All networking will happen over TCP/UDP (without authentication or encryption) in order to allow us to spend more time on throughput optimization. 
- Log Compaction. This is necessary for long running systems, but not for this project. 
- Handling disk corruption. This is also necessary for production systems, but is not extremely pressing for our proof of concept (especially as many disk controllers already implement checksum validation to some extent). 
- Since our clients are sending commands via UDP, we're going to limit the size of each command to 1024 bytes (as not to overflow the max size of a UDP frame). This issue could be solved with a more advanced networking protocol, but that's out of scope for this assignment. 

## RSM State
- The state that is maintained by an append only log file. 
```c
// 40 bytes (8 byte aligned)
struct LogEntryHeader {
    uint64_t term;
    uint64_t num_commands; // The number of commands stored at this log index (this allows us to batch appendEntries calls)
    uint64_t log_index;
    size_t command_length;  // The Length of all the commands in bytes. 
    uint8_t header_hash[16];  // Hash of the header. If the hashes don't line up, then we assume it was because of a partial write. 
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
// Arbitrary length (8 byte aligned)
struct Command {
    uint8_t command;        // (enum with NOP (code = 0), GET (code = 1), SET (code = 2), DELETE (code = 3)) (GET is only included as a formality as it doesn't change state)
    char key[...];          // Null terminated string
    uint64_t value_length;  // length of the value (note, the key string needs to be padded so that this field can be 8 byte aligned).
    uint8_t value[...];
}
```

## Key Value Structure
- In addition to the append only log file, we also need to actually maintain a hashmap in order to service get commands without iterating though the entire log file.
- In order to maintain a HashMap on disk, we will need to make each entry fixed length. 
- A hashmap entry be stored as is shown below. The `cmd_byte_offset` is the byte offset of the relevant SET command in the commands log file. We can get the fill key and value by following this byte offset.
- Possible KV optimization => We don't bother comparing the keys if the hashes match, we just assume the keys match if the hashes match. The possibility of a collision even with 1 billion records is 1 in 250 quadrillion. 
```c
// 24 bytes (8 byte aligned) (v3.2) (same logical struct, but we store these in a SoA format in the first 3072 bytes of the page)
struct HashMapEntry {
    uint64_t cmd_byte_offset_and_inline_value_slot;   // UINT64_MAX if the field is empty (upper 56 bits is the cmd_byte_offset while the lower 8 bits is the inline_value_slot)
    uint8_t key_hash[16];
}
```


- The hashmap file will also store the following metadata in the last page. 
```c
// 32 bytes
struct HashMapMetadata {
    uint64_t last_committed_index;
    uint64_t last_committed_term;
    uint64_t num_slots;
    uint64_t num_entries;
    uint8_t bloom_filter[];
}
```

- We will also maintain a bloom filter to possibly skip disk reads. This bloom filter will take up the remaining 4064 bytes in the first page. 
- **Inline Value store optimization** => We group each slot into a page (4096 bytes).
    - Page Format:
        - bytes 0 - 3072 => Store 128 entry slots
        - bytes 3072 - 4032 => Stores 15 slots for inline values stores that are 64 bytes each.
        - bytes 4032 - 4072 => `pthread_mutex_t` type (embedded lock).
        - bytes 4072 - 4074 => packed bitmap of value slots. 
        - bytes 4074 - 4096 => Unused
    - The inline value slot will have a length (byte 63 for 64 bytes) and the first 63 bytes will be used to store the actual data. 

- **Rehashing** => TBD. 
- Servicing GET, SET, and DELETE operations will have roughly n/2 threads working on this where n is the number of logical cores on our machine.


## Handling Client Requests
- We will have n threads handling client responses where n is the number of logical lores on our system. They will each have a buffer where they input the commands that they receive from clients. These commands will be 32 byte aligned. After 50 ms, we will consider this batch to be complete, and will start copying them over into a unified buffer. We can then surround this buffer with the correct headers, and replicate it to all follower nodes. After receiving a success message from the majority of our followers, we can simply dump this buffer into the append only log file as it's already in the correct format. 
- This will have roughly n/2 threads working on this where n is the number of logical cores on our machine.  

## Network Protocol
- Clients will send messages to the leader over UDP. This is because a process can only have 4096 files open (including TCP connections) at once. Since we're trying to create an extremely high throughput implementation of raft, it's likely that we will need to service more than 4K clients at a time. 
- The nodes in the cluster will communicate with each other using TCP. 
- The ports used for internal connections (TCP) will start at 8000. The ports used for client requests start at 9000.

## Raft Optimizations
- Our implementation will include batching of AppendEntries requests to minimize the number of RPC calls and improve throughput.  Each batch of commands, once formed, is replicated in a single AppendEntries message. The `num_commands` and `command_length` fields in the log header are used to efficiently decode the batch during replay.

 ```c
 // Constructing a batch in memory before replication
struct CommandBatch {
    uint32_t num_commands;
    uint64_t total_length;
    uint8_t commands[MAX_COMMAND_BUFFER]; // dynamically filled
};
 ```
- To further improve throughput, we will dynamically adjust the Raft election timeout using recent leader stability metrics. If no failures are detected for a period of time, the timeout increases to avoid unnecessary elections. If failure is suspected (e.g., missed heartbeats), the timeout will be shortened to ensure quick leader recovery.

```c
// Adaptive election timeout adjustment (pseudo-code)
if (leaderStableFor >= STABLE_THRESHOLD) {
    election_timeout = min(election_timeout * 2, MAX_TIMEOUT);
} else if (missedHeartbeats >= HEARTBEAT_THRESHOLD) {
    election_timeout = max(election_timeout / 2, MIN_TIMEOUT);
}
```
- For additional performance, if time permits, we will explore compression of batched AppendEntries requests when bandwidth usage becomes a bottleneck and CPU remains underutilized. Similarly, we may implement command deduplication for SET commands that repeatedly target the same key in a batch.
- To ensure efficient handling of GET requests, especially under high load, the fixed-length HashMap structure enables quick access to key metadata without scanning the full command log. The Bloom filter provides a fast, probabilistic way to skip disk lookups when a key is definitely not present.
- Performance and correctness under failure will be tested across a range of hardware setups and workloads, and leader election patterns will be monitored to ensure adaptive timeout logic behaves as expected.



## Status Codes
- 200 => operation successful
- 201 => get(k) operation succeeded, but no key `k` exists
- 500 => Operation failed