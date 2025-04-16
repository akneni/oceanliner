# Raft Consensus Implementation

## Overview

Implementing the core Raft consensus protocol with:
- Leader election
- Log replication
- State machine application
- Safety guarantees per the Raft paper

## Features

- Complete Raft state machine (Follower, Candidate, Leader states)
- Log entry management and consistency checks
- RPC handlers for AppendEntries and RequestVote
- Thread-safe design with proper mutex handling
- State machine interface for applying committed entries
- Comprehensive test suite

## Building

```bash
# Build the project
make

# Run tests
make test

# Clean build artifacts
make clean
```

## Components

- **Core:** State transition, term management, log operations
- **RPCs:** AppendEntries (heartbeats/replication), RequestVote (elections)
- **State Machine:** Key-value store that applies committed log entries

## Current Status

- Core Raft algorithm implemented and tested
- Integration with key-value store in progress
- Network layer not yet implemented

## Next Steps

- Complete state machine integration
- Add network communication
- Implement persistence
- Support cluster configuration changes