# Write-Ahead Log (WAL) & Crash Durability

## 1. WAL Protocol Discipline
* **Format:** Append-only log file (`db.log`) formatted with structured records:
  * `BEGIN,<txn_id>`
  * `SET,<txn_id>,<key>,<val>`
  * `DELETE,<txn_id>,<key>`
  * `COMMIT,<txn_id>`
* **Write Ordering:** All state modifications are flushed to the log before modifying in-memory hash maps. Standalone mutations outside explicit transaction blocks execute as atomic single-operation transactions (`BEGIN -> SET/DELETE -> COMMIT`).

---

## 2. Replay & Crash Recovery State Machine
* **In-Flight Tracking:** `Database::recover()` reads `db.log` sequentially on boot, tracking active transactions in an in-memory staging table (`pendingTransactions`).
* **Uncommitted Transaction Discard:** If the process crashes before a `COMMIT` entry is written, staged operations for that transaction ID are discarded, preserving the Atomicity (A) and Durability (D) invariants of ACID.
* **Tombstone Replay:** Explicit `DELETE` records replay as `data.erase(key)`, preventing deleted keys from resurrecting during post-crash restarts.
