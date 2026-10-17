# Project Flowchart — `MiniDB`

These diagrams explain the **implemented control flow**.

## How to read this
- **Rectangle**: operation / component.
- **Diamond**: decision.
- **Solid arrows**: normal control or data flow.
- The Mermaid diagrams render on GitHub and in VS Code with a Mermaid preview extension.
- Start with each project's **big picture**, then follow the request/transaction flow.

---

# `MiniDB`

## 1. Big-picture architecture

```mermaid
flowchart TB
    APP[src/main.cpp<br/>demonstration workload] --> API[Database API<br/>begin / get / set / erase / commit / rollback]
    API --> STACK[Per-thread transaction stack]
    API --> MAP[Committed in-memory key-value map]
    API --> VALID[Outermost commit validation]
    API --> WAL[Logger / append-only db.log]
    WAL --> REC[Database::recover at construction]
    REC --> MAP
    TP[ThreadPool<br/>concurrent demo tasks] --> APP
    LOCK[shared_mutex] -. protects shared committed state .-> MAP
    LOCK -. coordinates validation and writes .-> VALID
    LOG[Logger mutex + flush] -. serializes WAL writes .-> WAL
```

**Mental model:** each thread gets its own stack of transaction frames. Writes are staged in a transaction's pending changes; reads check the current thread's nested frames before committed data. An outermost commit validates the original values, records the commit in the WAL, and then applies the changes to memory.

## 2. Startup and crash recovery

```mermaid
flowchart TD
    START([main starts]) --> LOGGER[Open db.log in append mode]
    LOGGER --> DB[Construct Database]
    DB --> RECOVER[Database::recover reads WAL sequentially]
    RECOVER --> RECORD{Record type?}
    RECORD -->|BEGIN,id| BEGIN[Create pending transaction staging area<br/>advance next transaction ID]
    RECORD -->|SET,id,key,value| SET[Stage key = value for that transaction]
    RECORD -->|DELETE,id,key| DEL[Stage key deletion / tombstone]
    RECORD -->|COMMIT,id| COMMIT[Find staged changes for id]
    COMMIT --> APPLY[Apply staged changes to in-memory map]
    APPLY --> REMOVE[Remove transaction from pending staging]
    BEGIN --> NEXT{More WAL records?}
    SET --> NEXT
    DEL --> NEXT
    REMOVE --> NEXT
    NEXT -->|Yes| RECORD
    NEXT -->|No| DISCARD[Discard any still-pending uncommitted transactions]
    DISCARD --> READY([Database ready for API calls])
```

**Why the WAL matters:** committed records can rebuild the in-memory map after restart. A transaction without a `COMMIT` record remains staged and is not applied. `DELETE` must replay as an actual erase so deleted keys do not reappear.

## 3. `get(key)` — read-your-own-writes

```mermaid
flowchart TD
    START([get key]) --> LOCK[Acquire shared access to database state]
    LOCK --> STACK{Does this thread have active transactions?}
    STACK -->|Yes| INNER[Search transaction stack from innermost to outermost]
    INNER --> PENDING{A pending change for key found?}
    PENDING -->|Yes: value| VALUE[Return staged value]
    PENDING -->|Yes: deletion tombstone| MISSING[Return not-found / empty result]
    PENDING -->|No| MAP[Look up key in committed map]
    STACK -->|No| MAP
    MAP --> FOUND{Key exists?}
    FOUND -->|Yes| RETURN[Return committed value]
    FOUND -->|No| MISSING
    VALUE --> END([Return])
    MISSING --> END
    RETURN --> END
```

A transaction sees its own staged updates, including updates in nested frames, without exposing those uncommitted values to other threads.

## 4. `set(key, value)` and `erase(key)`

```mermaid
flowchart TD
    START([set or erase]) --> TXN{Current thread has active transaction?}
    TXN -->|Yes| SNAP[Capture original visible value once for rollback / validation]
    SNAP --> STAGE[Store latest pending value or deletion tombstone in top frame]
    STAGE --> DONE([Return; shared map unchanged])
    TXN -->|No| IMPLICIT[Create implicit single-operation transaction]
    IMPLICIT --> SNAP2[Capture original state]
    SNAP2 --> STAGE2[Stage value or tombstone]
    STAGE2 --> COMMIT[Run outermost commit protocol]
    COMMIT --> RESULT{Validation and WAL write succeed?}
    RESULT -->|Yes| APPLY[Apply mutation to committed map]
    RESULT -->|No| ABORT[Discard staged change]
    APPLY --> END([Return])
    ABORT --> END
```

`set` and `erase` share the transaction machinery. A standalone mutation is treated as a small transaction rather than bypassing commit logic.

## 5. `commit()` — nested versus outermost transaction

```mermaid
flowchart TD
    START([commit]) --> ACTIVE{Current thread has active transaction?}
    ACTIVE -->|No| NOOP[No active transaction to commit]
    ACTIVE -->|Yes| POP[Pop top transaction frame]
    POP --> PARENT{Is there a parent frame?}
    PARENT -->|Yes: nested commit| MERGE[Merge child's pending changes into parent]
    MERGE --> PRESERVE[Preserve parent's earliest original snapshots]
    PRESERVE --> NESTEDEND([Nested commit complete<br/>nothing applied to shared map yet])
    PARENT -->|No: outermost commit| VALIDATE[Validate current committed values against saved originals]
    VALIDATE --> CONFLICT{Any conflicting key changed?}
    CONFLICT -->|Yes| ABORT[Discard pending changes; report/abort conflict]
    CONFLICT -->|No| WALBEGIN[Append BEGIN and mutation records to WAL]
    WALBEGIN --> WALCOMMIT[Append COMMIT record and flush]
    WALCOMMIT --> APPLY[Apply staged values/deletions to committed map]
    APPLY --> END([Commit complete])
    NOOP --> END
    ABORT --> END
```

**Critical concept:** a nested commit is a merge into its parent savepoint, not a durable database commit. Only the outermost frame validates and publishes changes to the shared map.

## 6. `rollback()` — nested savepoint behavior

```mermaid
flowchart TD
    START([rollback]) --> ACTIVE{Active transaction frame?}
    ACTIVE -->|No| NOOP[Nothing to roll back]
    ACTIVE -->|Yes| POP[Pop innermost frame]
    POP --> DISCARD[Discard that frame's pending changes]
    DISCARD --> PARENT{Parent transaction remains?}
    PARENT -->|Yes| PRESERVE[Keep parent frame and its staged changes]
    PRESERVE --> NESTED([Return to parent transaction])
    PARENT -->|No| CLEAN[Remove empty per-thread transaction-stack entry]
    CLEAN --> END([No active transaction remains])
    NOOP --> END
```

Rollback is scoped to the current thread's innermost transaction.

## 7. Concurrency and task execution

```mermaid
flowchart TD
    START([main]) --> INIT[Create Logger, Database, ThreadPool]
    INIT --> TASKS[Enqueue independent transaction tasks]
    TASKS --> WORKERS[Worker threads pull tasks from queue]
    WORKERS --> BEGIN[begin]
    BEGIN --> OPS[set / get / erase stage or read values]
    OPS --> DECISION{Task's scenario?}
    DECISION -->|Commit path| COMMIT[commit: nested merge or outer validation + WAL]
    DECISION -->|Rollback path| ROLLBACK[rollback: discard innermost frame]
    COMMIT --> PRINT[Print task result under output mutex]
    ROLLBACK --> PRINT
    PRINT --> NEXT[Worker takes next queued task]
    NEXT --> WORKERS
    INIT --> READTASKS[Enqueue reader tasks]
    READTASKS --> GET[get committed value or thread-local staged value]
    GET --> PRINTREAD[Print read result]
```

The current `main.cpp` is a concurrency demonstration: it queues transaction tasks and reader tasks on a worker pool. It is not a command-line database shell.

## 8. Source-file responsibility map

```mermaid
flowchart LR
    MAIN[src/main.cpp<br/>demo workload] --> DB[src/Database.cpp<br/>API + transaction orchestration + recovery]
    DB --> TXN[src/Transaction.cpp / Transaction.h<br/>transaction frame data]
    DB --> LOGGER[src/Logger.cpp<br/>WAL record persistence]
    MAIN --> POOL[src/ThreadPool.cpp<br/>concurrent task execution]
    HEADERS[include/*.h<br/>interfaces and state] -. contracts .-> DB
    HEADERS -. contracts .-> POOL
```

---

# Suggested reading order

1. `src/main.cpp` — see the actual workload and concurrency setup.
2. `include/Database.h` and `include/Transaction.h` — understand state and transaction frames.
3. `src/Database.cpp` — follow `get`, `set`, `erase`, `begin`, `commit`, `rollback`, and `recover`.
4. `src/Logger.cpp` — WAL append and flush behavior.
5. `src/ThreadPool.cpp` — how concurrent tasks are scheduled.
6. `docs/TRANSACTION_INTERNALS.md` and `docs/WAL_RECOVERY.md` — use these as supporting explanations.

## Important interpretation note
- These are **code-understanding flowcharts**, not a certification that every advertised property is fully correct under every failure mode.
