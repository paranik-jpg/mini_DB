#include "Database.h"

Database::Database(Logger& logger) : logger(logger) {
    recover();
}

// thread id finder
std::thread::id Database::currentThread() {
    return std::this_thread::get_id();
}

void Database::recover() {
    std::ifstream LogFile("db.log");
    std::string line;

    using Changes = std::unordered_map<std::string, std::optional<std::string>>;

    std::unordered_map<std::uint64_t, Changes> pendingTransactions;

    std::lock_guard<std::shared_mutex> lock(mtx);

    while(std::getline(LogFile, line)) {
        std::stringstream ss(line);

        std::string type;
        if (!std::getline(ss, type, ',')) {
            continue;
        }

        if (type == "BEGIN") {
            std::string idText;
            if (!std::getline(ss, idText)) {
                continue;
            }

          auto id = std::stoull(idText);
          pendingTransactions.try_emplace(id);

          if (id >= next_transaction_id) {
            next_transaction_id = id + 1;
          }
        }
        else if (type == "SET" || type == "DELETE") {
            std::string idText;
            std::string key;

          if (!std::getline(ss, idText, ',') || !std::getline(ss, key, ',')) {
            continue;
          }

          auto id = std::stoull(idText);
          auto it = pendingTransactions.find(id);

          if (it == pendingTransactions.end()) {
            continue;
          }

          if (type == "SET") {
            std::string value;
            if(!std::getline(ss, value)) {
              continue;
            }

            it->second[key] = value;
          } else {
            it->second[key] = std::nullopt;
          }
        }
        else if (type == "COMMIT") {
            std::string idText;
            if (!std::getline(ss, idText)) {
                continue;
            }

            auto id = std::stoull(idText);
            auto it = pendingTransactions.find(id);

            if (it == pendingTransactions.end()) {
                continue;
            }

            for (const auto& [key, value] : it->second) {
                if (value.has_value()) {
                    data[key] = *value;
                } else {
                  data.erase(key);
                }
            }

            pendingTransactions.erase(it);
        }
    }
}

bool Database::isTransactionActive () {
    std::shared_lock<std::shared_mutex> lock(mtx);

    // this_thread::get_id() returns current thread's id
    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    // auto& txnStack = transaction_stacks[std::this_thread::get_id()]; omitted becoz this will create a stack if not available

    if(it == transaction_stacks.end()) { // if id not found
        return false;
    }

    return !it->second.empty();          // if id found, check for emptiness
}

void Database::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    // This will not create extra vac memo
    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    if (it != transaction_stacks.end() && !it->second.empty()) {
        auto& current_txn = it->second.top();                               // If transaction stack is not empty, we are extracting the top one

        // Save the original visible value only once.
        if(current_txn.undo_log.find(key) == current_txn.undo_log.end()) {  // Not found in logs
            std::optional<std::string> oldValue;
            bool foundPendingValue = false;

            // Search active transactions, starting with the innermost.
            auto txnStack = it->second;

            while (!txnStack.empty()) {
                const auto& txn = txnStack.top();
                auto redoIt = txn.redo_log.find(key);

                if (redoIt != txn.redo_log.end()) {
                    oldValue = redoIt->second;
                    foundPendingValue = true;
                    break;
                }

                txnStack.pop();
            }

            // If no transactions has a pending value, check the database.
            if (!foundPendingValue) {
              auto data_it = data.find(key);

              if (data_it != data.end()) {
                oldValue = data_it->second;
              }
            }

            current_txn.undo_log[key] = oldValue;
        }

        // Store the latest value for eventual commit.
        current_txn.redo_log[key] = value;
    } else {
        // Create an implicit transaction for a standalone write.
        Transaction txn;
        txn.id = next_transaction_id++;

        logger.logRecord("BEGIN," + std::to_string(txn.id));
        logger.logRecord(
            "SET," + std::to_string(txn.id) + "," + key + "," + value
        );
        logger.logRecord("COMMIT," + std::to_string(txn.id));

        data[key] = value;
    }
}

void Database::erase(const std::string& key) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    // Case 1: An active transaction exists.
    if (it != transaction_stacks.end() && !it->second.empty()) {
        auto& current_txn = it->second.top();

        // Save the original visible value only once.
        if (current_txn.undo_log.find(key) == current_txn.undo_log.end()) {
          std::optional<std::string> oldValue;
          bool foundPendingValue = false;

          // Search inner-to-outer for a pending value.
          auto txnStack = it->second;

          while (!txnStack.empty()) {
            const auto& txn = txnStack.top();
            auto redoIt = txn.redo_log.find(key);

            if (redoIt != txn.redo_log.end()) {
              oldValue = redoIt->second;
              foundPendingValue = true;
              break;
            }

            txnStack.pop();
          }

          // Fall back to the actual database.
          if (!foundPendingValue) {
            auto dataIt = data.find(key);

            if (dataIt != data.end()) {
                oldValue = dataIt->second;
            }
          }

          current_txn.undo_log[key] = oldValue;
        }

        // nullopt represents a pending deletion.
        current_txn.redo_log[key] = std::nullopt;
    } else {
        // Create an implicit transaction for a standalone deletion.
        Transaction txn;
        txn.id = next_transaction_id++;

        logger.logRecord("BEGIN," + std::to_string(txn.id));
        logger.logRecord(
            "DELETE," + std::to_string(txn.id) + "," + key
        );
        logger.logRecord("COMMIT," + std::to_string(txn.id));

        data.erase(key);
    }
}

std::string Database::get(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(mtx);

    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    if (it != transaction_stacks.end() && !it->second.empty()) {
      auto txnStack = it->second;

      while (!txnStack.empty()) {
        const auto& txn = txnStack.top();
        auto redoIt = txn.redo_log.find(key);

        if (redoIt != txn.redo_log.end()) {
            if (redoIt->second.has_value()) {
              return *redoIt->second;
            }

            return "NULL";
        }

        txnStack.pop();
      }
    }

    auto dataIt = data.find(key);

    if (dataIt != data.end()) {
      return dataIt->second;
    }

    return "NULL";
}

bool Database::commit() {
    std::lock_guard<std::shared_mutex> lock(mtx);

    // This will not create extra vac memo
    auto id = currentThread();
    auto it = transaction_stacks.find(id);


    if(it == transaction_stacks.end() || it->second.empty()) {
        return false;
    }

    // Remove the current transaction from the stack.
    auto child = std::move(it->second.top());
    it->second.pop();

    // Nested commit: merge into the parent.
    if(!it->second.empty()) {
        auto& parent = it->second.top();

        // Preserve the parent's original state for rollback.
        for(const auto& [key, oldValue] : child.undo_log) {
            if(parent.undo_log.find(key) == parent.undo_log.end()) {
                parent.undo_log[key] = oldValue;
            }
        }

        // The child's latest values become the parent's latest values.
        for(const auto& [key, newValue] : child.redo_log) {
            parent.redo_log[key] = newValue;
        }

        return true;
    }

    // Validate before committing.
    for (const auto& [key, oldValue] : child.undo_log) {
      std::optional<std::string> currentValue;

      auto dataIt = data.find(key);
      if (dataIt != data.end()) {
        currentValue = dataIt->second;
      }

      if (currentValue != oldValue) {
        // Conflict detected: discard this transaction.
        transaction_stacks.erase(it);
        return false;
      }
    }

    // Log the final changes of the outermost transaction.
    for (const auto& [key, newValue] : child.redo_log) {
        if (newValue.has_value()) {
            logger.logRecord(
                "SET," + std::to_string(child.id) + "," + key + "," + *newValue
            );
        } else {
            logger.logRecord(
                "DELETE," + std::to_string(child.id) + "," + key
            );
        }
    }

    // Record the commit before applying changes to memory.
    logger.logRecord("COMMIT," + std::to_string(child.id));

    // Apply the committed changes.
    for (const auto& [key, newValue] : child.redo_log) {
        if (newValue.has_value()) {
          data[key] = *newValue;
        } else {
          data.erase(key);
        }
    }

    // The transaction stack is now empty.
    transaction_stacks.erase(it);
    return true;
}

// Begin a new transaction
void Database::begin() {
    std::lock_guard<std::shared_mutex> lock(mtx);

    Transaction txn;
    txn.id = next_transaction_id++;

    // Record the transaction's beginning in the WAL.
    logger.logRecord("BEGIN," + std::to_string(txn.id));

    // Push the transaction onto this thread's stack.
    transaction_stacks[currentThread()].push(std::move(txn));
}

// Rollback to the last transaction
void Database::rollback() {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    // No active transaction for this thread.
    if(it == transaction_stacks.end() || it->second.empty()) {
      return;
    }

    // Remove the rolled-back transaction.
    it->second.pop();

    // Remove the thread's entry if no transactions remain.
    if(it->second.empty()) {
        transaction_stacks.erase(it);
    }
}
