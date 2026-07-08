#include "Database.h"

#include <sstream>

Database::Database(Logger& logger) : logger(logger) {
    recover();
}

// thread id finder
std::thread::id Database::currentThread() {
    return std::this_thread::get_id();
}

void Database::recover() {
    std::ifstream tempLogFile("db.log");
    std::string line;

    // Lock the mutex because we are modifying 'data' during recovery
    std::lock_guard<std::mutex> lock(mtx);
    while(std::getline(tempLogFile, line)) {
        std::stringstream ss(line);
        std::string key, value;

        // Read until the comma
        if(std::getline(ss, key, ',') && std::getline(ss, value)) {
            data[key] = value; // Reconstruct the state
        }
    }
}

bool Database::isTransactionActive () {
    std::lock_guard<std::mutex> lock(mtx);

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
    logger.log(key, value); // Storing in file 
    {
        std::lock_guard<std::mutex> lock(mtx);

        // This will not create extra vac memo
        auto id = currentThread();
        auto it = transaction_stacks.find(id);

        if (it != transaction_stacks.end() && !it->second.empty()) {
            auto& current_txn = it->second.top();                              // If transaction stack is not empty, we are extracting the top one
            if(current_txn.undo_log.find(key) == current_txn.undo_log.end()) { // Not found in logs
                if(data.find(key) != data.end()) {                             // Found in data
                    current_txn.undo_log[key] = data[key];                     // Backup
                } else {
                    current_txn.undo_log[key] = "__DELETE__";                  // This will be deleted on rollback, becoz we created this key, new 
                }
            }
        }
        data[key] = value;
    }
}

std::string Database::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = data.find(key);

    if(it != data.end()) {
        return it->second;
    }

    return "NULL";
}

void Database::commit() {
    std::lock_guard<std::mutex> lock(mtx);

    // This will not create extra vac memo
    auto id = currentThread();
    auto it = transaction_stacks.find(id);


    if(it == transaction_stacks.end() || it->second.empty()) {
        return;
    }

    // If only one transaction
    if(it->second.size() == 1) {
        it->second.pop();                 // Remove the top element

        if(it->second.empty()) {          // If stack becomes empty, erase the whole stack
            transaction_stacks.erase(it);
        }
    } 
    else {
        // Save the child
        auto child = std::move(it->second.top());
        it->second.pop();

        auto& parent = it->second.top();

        for(const auto& [key, oldValue] : child.undo_log) {
            if(parent.undo_log.find(key) == parent.undo_log.end()) {
                parent.undo_log[key] = oldValue;
            }
        }
    }
}

// Begin a new transaction
void Database::begin() {
    std::lock_guard<std::mutex> lock(mtx);

    auto& txnStack = transaction_stacks[currentThread()];

    // emplace() constructs the object directly inside the stack (No temp copy)
    txnStack.emplace(); 
}

// Rollback to the last transaction
void Database::rollback() {
    std::lock_guard<std::mutex> lock(mtx);
    auto id = currentThread();
    auto it = transaction_stacks.find(id);
    if(it != transaction_stacks.end() && !it->second.empty()) {
        auto& current_txn = it->second.top(); // If transaction stack is not empty, we are extracting the top one
            
        // Revert changes from the log
        for(auto const& [key, old_val] : current_txn.undo_log) {
            if (old_val == "__DELETE__") {
                data.erase(key);             // If not present earlier, remove now
            } else {
                data[key] = old_val;         // Restoring the old value
            }
        }
        it->second.pop();                    // Removal after rollback from stack

        if(it->second.empty()) {
            transaction_stacks.erase(it);    // Removal of whole stack
        }
    }
}
