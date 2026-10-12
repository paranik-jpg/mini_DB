# Transaction Engine Internals & Concurrency Control

## 1. Concurrency Architecture
* **Fine-Grained vs. Shared Locking:** Uses `std::shared_mutex` to allow concurrent readers via `std::shared_lock` while isolating writers with exclusive `std::lock_guard` acquisition.
* **Optimistic Local Buffering (OCC):** Transactions buffer mutations in a local `redo_log` and snapshot historical states in an `undo_log` rather than dirtying the shared data map immediately.
* **Dirty Read Mitigation:** Reader operations (`Database::get`) evaluate the calling thread's active transaction redo stack before falling back to the committed data map, guaranteeing read-your-own-writes isolation without exposing uncommitted data to peer threads.

---

## 2. Nested Transactions & Savepoints
* **Stack Framing:** Each thread maintains a LIFO stack (`std::stack<Transaction>`) keyed by `std::thread::id`.
* **Merge Semantics on Commit:** Inner transaction commits merge their changes into the immediate parent frame, preserving the parent's original pre-transaction snapshots.
* **Rollback Isolation:** Popping an inner frame discards pending mutations and restores key snapshots without aborting the enclosing transaction context.

---

## 3. Conflict Detection & Outermost Validation
* **Validation Phase:** Before committing outermost transactions, the engine validates that the current database values match the snapshot values captured in `undo_log`.
* **Resolution:** If a concurrent thread committed conflicting writes during the transaction lifespan, the transaction discards its pending writes and aborts cleanly.
