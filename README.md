# MiniDB: Embedded C++17 Transactional Key-Value Engine

MiniDB is an in-memory key-value database engine written in modern C++17. It explores transaction management, optimistic concurrency control (OCC), nested savepoints, write-ahead logging (WAL) with hex encoding, crash recovery, and concurrent task execution through a bounded worker thread pool.

The project emphasizes modular design, resource safety, and understanding the internal mechanisms behind transactional storage engines.

## Table of Contents

- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Features](#features)
- [Project Structure](#project-structure)
- [Requirements](#requirements)
- [Build, Run, and Test](#build-run-and-test)
- [Memory and Resource Verification](#memory-and-resource-verification)
- [Documentation](#documentation)
- [Current Limitations and Roadmap](#current-limitations-and-roadmap)
- [License](#license)

## Architecture

```text
                     ┌─────────────────────────────┐
                     │         Database API        │
                     │ begin / set / erase / get   │
                     │          / commit           │
                     └──────────────┬──────────────┘
                                    │
                  ┌─────────────────┴───────────────────┐
                  ▼                                     ▼
       ┌──────────────────────┐             ┌──────────────────────┐
       │ Transaction Manager  │             │     Write-Ahead Log  │
       │                      │             │                      │
       │ Nested Savepoints    │             │ Structured Records   │
       │ Undo Log / Redo Log  │             │ Hex Encoding         │
       │ Read-Set Validation  │             │ Crash Recovery       │
       └───────────┬──────────┘             └───────────┬──────────┘
                   │                                    │
                   └─────────────────┬──────────────────┘
                                     ▼
                       ┌─────────────────────────┐
                       │  In-Memory State Store  │
                       │                         │
                       │   std::unordered_map    │
                       │   std::shared_mutex     │
                       └─────────────────────────┘
                                     ▲
                                     │
                       ┌─────────────────────────┐
                       │   Bounded Thread Pool   │
                       │  Concurrent Execution   │
                       └─────────────────────────┘
```

The database API coordinates transaction operations, the logger records mutations for durability and recovery, and the in-memory store maintains committed key-value state. Synchronization and transaction validation protect shared state during concurrent operations.

## Quick Start

```cpp
#include "Database.h"
#include "Logger.h"
#include <iostream>

int main() {
    // 1. Initialize WAL logger and Database
    Logger logger("db.log");
    Database db(logger); // Automatically replays db.log if it exists

    // 2. Start a transaction
    db.begin();
    db.set("user:101", "Alice");

    // 3. Nested savepoint (inner transaction)
    db.begin();
    db.set("user:101", "Bob");
    db.rollback(); // Reverts inner write back to "Alice"

    // 4. Commit outer transaction
    if (db.commit()) {
        std::cout << "Transaction committed successfully.\n";
    }

    // 5. Query data (returns std::optional<std::string>)
    auto val = db.get("user:101");
    if (val) {
        std::cout << "user:101 = " << *val << "\n"; // Prints "Alice"
    }

    return 0;
}
```

## Features

### Transaction Management & Optimistic Concurrency Control (OCC)

- **Transaction-local buffering:** Mutations are buffered locally in an uncommitted `redo_log` and only applied to shared memory upon successful commit.
- **Read-your-own-writes isolation:** A transaction observes its own pending modifications by traversing its active transaction stack from innermost to outermost frames before reading committed state.
- **Read-set tracking and validation:** Reads observe committed data and record the values in a `read_set`. At commit time, the engine validates that read values and `undo_log` snapshots have not been modified concurrently, preventing stale or conflicting writes.
- **Nested savepoints:** Supports hierarchical transaction frames (`begin()` inside an active transaction). Popping an inner frame (`rollback()`) discards nested changes, while committing an inner frame merges pending changes into the parent frame.
- **Implicit transactions:** Standalone writes and deletions outside explicit transaction blocks are automatically wrapped in atomic single-operation transactions (`BEGIN` → `SET/DELETE` → `COMMIT`).

### Write-Ahead Logging & Crash Durability

- **Structured append-only logging:** Records operations (`BEGIN`, `SET`, `DELETE`, `COMMIT`) to `db.log`.
- **Hex encoding:** Keys and values in `SET` and `DELETE` records are encoded in hexadecimal format. This ensures that arbitrary binary data, commas, newlines, and empty strings can be safely recorded without delimiter ambiguity or parsing corruption.
- **Write ordering:** All mutations are appended to the WAL and flushed to disk before modifying in-memory state.
- **Fail-stop safety:** If a WAL write or flush fails (e.g. disk full), the database enters a fail-safe mode (`walFailed = true`) to prevent inconsistent state in memory.
- **Crash recovery:** On startup, `Database::recover()` sequentially replays `db.log`, staging operations in memory and applying only those transactions that completed with a `COMMIT` record. Incomplete or torn transactions at crash are cleanly discarded.

### Concurrency and Thread Pool

- **Reader-writer synchronization:** Uses `std::shared_mutex` to allow concurrent reader operations while isolating writers and commit validations.
- **Thread-isolated transaction frames:** Active transaction stacks are mapped by `std::thread::id`, preventing cross-thread pollution without relying on thread-local globals.
- **Bounded worker thread pool:** Custom `ThreadPool` with worker threads, bounded job queue (default capacity: 1000), condition variables, and timed backpressure.
- **Diagnostics & sanitizers:** Clean builds under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).

## Project Structure

```text
MiniDB/
├── docs/
│   ├── TRANSACTION_INTERNALS.md  # Transaction stack & OCC mechanics
│   ├── WAL_RECOVERY.md           # Log format and replay state machine
│   └── VERIFICATION.md           # Sanitizer and Valgrind verification logs
├── include/
│   ├── Database.h                # Main database engine interface
│   ├── Logger.h                  # Thread-safe WAL logger interface
│   ├── ThreadPool.h              # Bounded worker thread pool interface
│   └── Transaction.h             # Transaction frame definition
├── src/
│   ├── Database.cpp              # Engine logic, OCC validation, recovery
│   ├── Logger.cpp                # WAL append and flush implementation
│   ├── ThreadPool.cpp            # Thread pool queue and worker lifecycle
│   ├── Transaction.cpp           # Transaction translation unit
│   └── main.cpp                  # Concurrent multi-threaded demonstration
├── tests/
│   ├── wal_recovery_test.cpp     # Crash recovery & special character tests
│   ├── transaction_isolation_test.cpp # OCC concurrent conflict tests
│   └── wal_failure_test.cpp      # WAL write-failure injection tests
├── FLOWCHARTS.md                 # Mermaid architecture & control flow diagrams
├── CMakeLists.txt                # Build configuration and CTest setup
└── README.md
```

## Requirements

- C++17 compatible compiler (GCC 9+, Clang 10+)
- CMake 3.16+
- Make or Ninja
- Linux or POSIX-compatible environment
- Valgrind (optional, for memory and descriptor leak auditing)

## Build, Run, and Test

### 1. Build

```bash
# Configure build with sanitizers enabled
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON

# Compile executable and test suites
cmake --build build -j$(nproc)
```

### 2. Run the Concurrency Demo

```bash
./build/MiniDB
```
This runs 200 concurrent transactional tasks with nested savepoints and 50 concurrent reader threads on the thread pool.

### 3. Run Automated Tests

MiniDB includes automated integration tests executed via CTest:

```bash
ctest --test-dir build --output-on-failure
```

You can also run individual test binaries:
* `./build/MiniDB_WAL_Recovery` — Tests crash replay, newline/comma preservation, and uncommitted transaction discards.
* `./build/MiniDB_TransactionIsolation` — Tests concurrent OCC conflict detection and rollback isolation.
* `./build/MiniDB_WALFailure_Test` — Tests error handling when log writes fail (e.g., full disk).

## Memory and Resource Verification

### AddressSanitizer and UndefinedBehaviorSanitizer

Build with `-DENABLE_SANITIZERS=ON` and run both the demo and tests:

```bash
cmake -B build_asan -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build_asan -j$(nproc)
ctest --test-dir build_asan --output-on-failure
```

### Valgrind Memcheck

To audit for heap memory leaks or unclosed file descriptors:

```bash
valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-fds=yes \
  ./build/MiniDB
```

Expected result: `0 errors from 0 contexts` and `0 bytes in 0 blocks` in use at exit.

## Documentation

| Document | Purpose |
|---|---|
| [`FLOWCHARTS.md`](FLOWCHARTS.md) | Visual Mermaid flowcharts for architecture, `get`, `set`, `commit`, `rollback`, and crash recovery. |
| [`docs/TRANSACTION_INTERNALS.md`](docs/TRANSACTION_INTERNALS.md) | Details on transaction frames, undo/redo logs, nested savepoints, and OCC validation. |
| [`docs/WAL_RECOVERY.md`](docs/WAL_RECOVERY.md) | Log record schema, hex encoding, atomic write ordering, and crash recovery state machine. |
| [`docs/VERIFICATION.md`](docs/VERIFICATION.md) | Build configurations, sanitizer run results, and Valgrind memory leak verification. |

## Current Limitation

- **Log Compaction / Checkpointing:** The WAL is append-only. Adding checkpointing will allow truncating historical logs once committed state is persisted.

## License

This project is licensed under the MIT License.
