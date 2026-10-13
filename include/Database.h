#ifndef DATABASE_H
#define DATABASE_H

#include "Transaction.h"
#include "Logger.h"

#include <unordered_map>
#include <string>
#include <thread>
#include <vector>
#include <shared_mutex>
#include <cstdint>
#include <optional>
#include <sstream>

class Database {
private:
 std::unordered_map<std::string, std::string> data;

 // Separate stacks for different threads
 std::unordered_map<std::thread::id, std::vector<Transaction>>
     transaction_stacks;

 std::shared_mutex mtx;
 Logger& logger;

 bool walFailed = false;

 void ensureHealthy() const;
 void writeWalRecord(const std::string& record);

 std::uint64_t next_transaction_id = 1;

public:

    Database(Logger& logger);

    // thread id finder
    // std::thread::id is a type used to store 'id'
    static std::thread::id currentThread();

    void recover();

    bool isTransactionActive();

    void set(const std::string& key, const std::string& value);

    void erase(const std::string& key);

    std::optional<std::string> get(const std::string& key);

    bool commit();

    // Begin a new transaction
    void begin();

    // Rollback to the last transaction
    void rollback();
};


#endif
