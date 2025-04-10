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
// 64 bytes
struct LogEntryHeader {
    uint32_t term;
    uint32_t num_commands; // The number of commands stored at this log index (this allows us to batch appendEntries calls)
    uint64_t log_index;
    uint64_t command_length;  // The Length of all the commands in bytes. 
    uint8_t header_hash[24];  // Hash of the header. If the hashes don't line up, then we assume it was because of a partial write. 
    uint8_t magic_number[16];    // A random constant that we use to ensure that the 64 byte chunk in question is a header.
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
struct Command {
    uint8_t command;        // (enum with NOP, SET, DELETE, etc.) (GET not included as it doesn't change state)
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
struct HashMapEntry {
    uint64_t cmd_byte_offset;
    uint32_t key_length;        // Is this field is 0, then this slot is empty
    char string[18];
    uint8_t inline_value_slot;  // UINT8_MAX if value is not inlined
    uint8_t inline_value_len;
}
```

- The hashmap file will also store the following metadata in the first page. 
```c
// 32 bytes
struct HashMapMetadata {
    uint64_t last_committed_index;
    uint64_t last_committed_term;
    uint64_t num_slots;
    uint64_t num_entries;
}
```
- We will also maintain a bloom filter to possibly skip disk reads. This bloom filter will take up the remaining 4064 bytes in the first page. 

- **Inline Value store optimization** => We group each slot into a page (4096 bytes).
    - bytes 0 - 3200 => Store 100 KV slots (some of these slots may be unused in the last page to allow for power-of-2 sizing)
    - bytes 3200 - 3264 => Store packed bitmap of which inline value slots are taken. (we're only using 13 bits of the 64 bytes (512 bits) here, maybe we can find a use for the remaining bytes?)
    - bytes 3264 - 4096 => Stores 13 slots for inline values stores that are 64 bytes each. 

- **Rehashing** => TBD. 
- Servicing GET, SET, and DELETE operations will have roughly n/2 threads working on this where n is the number of logical cores on our machine. 


## Handling Client Requests
- We will have n threads handling client responses where n is the number of logical lores on our system. They will each have a buffer where they input the commands that they receive from clients. These commands will be 32 byte aligned. After 50 ms, we will consider this batch to be complete, and will start copying them over into a unified buffer. We can then surround this buffer with the correct headers, and replicate it to all follower nodes. After receiving a success message from the majority of our followers, we can simply dump this buffer into the append only log file as it's already in the correct format. 
- This will have roughly n/2 threads working on this where n is the number of logical cores on our machine.  

## Network Protocol
- Clients will send messages to the leader over UDP. This is because a process can only have 4096 files open (including TCP connections) at once. Since we're trying to create an extremely high throughput implementation of raft, it's likely that we will need to service more than 4K clients at a time. 
- The nodes in the cluster will communicate with each other using TCP. 

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

