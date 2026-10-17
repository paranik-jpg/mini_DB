#include "Database.h"
#include <stdexcept>

namespace {

constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

std::string encodeHex(const std::string& input) {
    std::string encoded;
    encoded.reserve(input.size() * 2);

    for (unsigned char byte : input) {
        encoded.push_back(HEX_DIGITS[byte >> 4]);
        encoded.push_back(HEX_DIGITS[byte & 0x0F]);
    }

    return encoded;
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

std::string decodeHex(const std::string& encoded) {
    if (encoded.size() % 2 != 0) {
        throw std::runtime_error("Invalid WAL hex encoding: odd length");
    }

    std::string decoded;
    decoded.reserve(encoded.size() / 2);

    for (std::size_t i = 0; i < encoded.size(); i += 2) {
        int high = hexValue(encoded[i]);
        int low = hexValue(encoded[i + 1]);

        if (high < 0 || low < 0) {
            throw std::runtime_error("Invalid WAL hex encoding: bad digit");
        }

        decoded.push_back(static_cast<char>((high << 4) | low));
    }

    return decoded;
}

} // namespace


Database::Database(Logger& logger) : logger(logger) {
    recover();
}

void Database::ensureHealthy() const {
    if (walFailed) {
        throw std::runtime_error(
            "Database is unavailable after a WAL write failure; reopen and recover it"
        );
    }
}

void Database::writeWalRecord(const std::string& record) {
    try {
        logger.logRecord(record);
    } catch (...) {
        walFailed = true;
        throw;
    }
}

// thread id finder
std::thread::id Database::currentThread() {
    return std::this_thread::get_id();
}

void Database::recover() {
    std::ifstream LogFile(logger.getFilename());
    if (!LogFile.is_open()) {
        throw std::runtime_error(
            "Failed to open WAL for recovery: " + logger.getFilename()
        );
    }

    std::string line;

    using Changes = std::unordered_map<std::string, std::optional<std::string>>;

    std::unordered_map<std::uint64_t, Changes> pendingTransactions;

    std::lock_guard<std::shared_mutex> lock(mtx);

    while (std::getline(LogFile, line)) {
        // A WAL record is valid only if it ends with a newline.
        // Ignore a potentially torn final record after a failed write.
        if (LogFile.eof()) {
            break;
        }

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

          // Makes sure future new transactions don't accidentally reuse an ID from the past.
          if (id >= next_transaction_id) {
            next_transaction_id = id + 1;
          }
        } else if (type == "SET" || type == "DELETE") {
            std::string idText;
            std::string keyHex;

            if (!std::getline(ss, idText, ',') ||
                !std::getline(ss, keyHex, ',')) {
                continue;
            }

            auto id = std::stoull(idText);
            auto it = pendingTransactions.find(id);

            if (it == pendingTransactions.end()) {
                continue;
            }

            std::string key = decodeHex(keyHex);

            if (type == "SET") {
                std::string valueHex;
                std::getline(ss, valueHex);
                it->second[key] = decodeHex(valueHex);
            } else {
                it->second[key] = std::nullopt;
            }
        } else if (type == "COMMIT") {
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
    ensureHealthy();

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
    ensureHealthy();

    // This will not create extra vac memo
    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    // Check if the current thread called begin()
    if (it != transaction_stacks.end() && !it->second.empty()) {
        // CASE 1: Inside an explicit transaction
        // Buffer into redo_log, do not touch `data` or WAL yet
        auto& current_txn = it->second.back();                               // If transaction stack is not empty, we are extracting the top one

        // Save the original visible value only once.
        if(current_txn.undo_log.find(key) == current_txn.undo_log.end()) {  // Not found in logs
            std::optional<std::string> oldValue;
            bool foundPendingValue = false;

            // Search active transactions, starting with the innermost.
            for (auto txnIt = it->second.rbegin(); txnIt != it->second.rend(); ++txnIt) {
                auto redoIt = txnIt->redo_log.find(key);
                if (redoIt != txnIt->redo_log.end()) {
                    oldValue = redoIt->second;
                    foundPendingValue = true;
                    break;
                }
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
        // CASE 2: OUTSIDE any transaction (Standalone write)
        // Autocommit / Implicit Transaction:
        Transaction txn;
        txn.id = next_transaction_id++;

        writeWalRecord("BEGIN," + std::to_string(txn.id));
        writeWalRecord(
            "SET," + std::to_string(txn.id) + "," +
            encodeHex(key) + "," + encodeHex(value)
        );
        writeWalRecord("COMMIT," + std::to_string(txn.id));

        data[key] = value;
    }
}

void Database::erase(const std::string& key) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    ensureHealthy();

    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    // Case 1: An active transaction exists.
    if (it != transaction_stacks.end() && !it->second.empty()) {
        auto& current_txn = it->second.back();

        // Save the original visible value only once.
        if (current_txn.undo_log.find(key) == current_txn.undo_log.end()) {
          std::optional<std::string> oldValue;
          bool foundPendingValue = false;

          // Search inner-to-outer without copying the transaction history.
          for (auto txnIt = it->second.rbegin(); txnIt != it->second.rend(); ++txnIt) {
            auto redoIt = txnIt->redo_log.find(key);
            if (redoIt != txnIt->redo_log.end()) {
              oldValue = redoIt->second;
              foundPendingValue = true;
              break;
            }
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

        writeWalRecord("BEGIN," + std::to_string(txn.id));
        writeWalRecord(
            "DELETE," + std::to_string(txn.id) + "," + encodeHex(key)
        );
        writeWalRecord("COMMIT," + std::to_string(txn.id));

        data.erase(key);
    }
}

std::optional<std::string> Database::get(const std::string& key) {
    // A transactional read may update the transaction's read set.
    std::lock_guard<std::shared_mutex> lock(mtx);
    ensureHealthy();

    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    if (it != transaction_stacks.end() && !it->second.empty()) {
        // Search from the innermost transaction outward.
        for (auto txnIt = it->second.rbegin(); txnIt != it->second.rend(); ++txnIt) {
            auto redoIt = txnIt->redo_log.find(key);
            if (redoIt != txnIt->redo_log.end()) {
                return redoIt->second;
            }
        }

        // No pending write shadows this key: record the value
        // observed from the committed database.
        std::optional<std::string> observedValue;
        auto dataIt = data.find(key);

        if (dataIt != data.end()) {
            observedValue = dataIt->second;
        }

        it->second.back().read_set.try_emplace(key, observedValue);

        return observedValue;
    }

    auto dataIt = data.find(key);

    if (dataIt != data.end()) {
        return dataIt->second;
    }

    return std::nullopt;
}

bool Database::commit() {
    std::lock_guard<std::shared_mutex> lock(mtx);
    ensureHealthy();

    // This will not create extra vac memo
    auto id = currentThread();
    auto it = transaction_stacks.find(id);


    if(it == transaction_stacks.end() || it->second.empty()) {
        return false;
    }

    // Keep the transaction on the stack until its commit succeeds.
    auto& child = it->second.back();

    // Nested commit: merge into the parent.
    if (it->second.size() > 1) {
        Transaction childTxn = std::move(it->second.back());
        it->second.pop_back();

        auto& parent = it->second.back();

        // Preserve the parent's original state for rollback.
        for (const auto& [key, oldValue] : childTxn.undo_log) {
            if (parent.undo_log.find(key) == parent.undo_log.end()) {
                parent.undo_log[key] = oldValue;
            }
        }

        // Merge the child's observed values without overwriting
        // observations already made by the parent.
        for (const auto& [key, observedValue] : childTxn.read_set) {
            parent.read_set.try_emplace(key, observedValue);
        }

        // The child's latest values become the parent's latest values.
        for (const auto& [key, newValue] : childTxn.redo_log) {
            parent.redo_log[key] = newValue;
        }

        return true;
    }


    // Validate every value observed by this transaction.
    for (const auto& [key, observedValue] : child.read_set) {
        std::optional<std::string> currentValue;

        auto dataIt = data.find(key);
        if (dataIt != data.end()) {
            currentValue = dataIt->second;
        }

        if (currentValue != observedValue) {
            // A value read by this transaction has changed.
            // Abort the transaction rather than commit stale decisions.
            transaction_stacks.erase(it);
            return false;
        }
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
            writeWalRecord(
                "SET," + std::to_string(child.id) + "," +
                encodeHex(key) + "," + encodeHex(*newValue)
            );
        } else {
            writeWalRecord(
                "DELETE," + std::to_string(child.id) + "," +
                encodeHex(key)
            );
        }
    }

    // Record the commit before applying changes to memory.
    writeWalRecord("COMMIT," + std::to_string(child.id));

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
    ensureHealthy();

    Transaction txn;
    txn.id = next_transaction_id++;

    // Record the transaction's beginning in the WAL.
    writeWalRecord("BEGIN," + std::to_string(txn.id));

    // Push the transaction onto this thread's stack.
    transaction_stacks[currentThread()].push_back(std::move(txn));
}

// Rollback to the last transaction
void Database::rollback() {
    std::lock_guard<std::shared_mutex> lock(mtx);
    ensureHealthy();

    auto id = currentThread();
    auto it = transaction_stacks.find(id);

    // No active transaction for this thread.
    if(it == transaction_stacks.end() || it->second.empty()) {
      return;
    }

    // Remove the rolled-back transaction.
    it->second.pop_back();

    // Remove the thread's entry if no transactions remain.
    if(it->second.empty()) {
        transaction_stacks.erase(it);
    }
}
