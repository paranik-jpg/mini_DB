#ifndef DATABASE_H
#define DATABASE_H

#include "Transaction.h"
#include "Logger.h"

#include <unordered_map>
#include <string>
#include <thread>
#include <stack>
#include <mutex>

class Database {
private:
    std::unordered_map<std::string, std::string> data;

    // Separate stacks for different threads
    // std::thread::id is a type used to store 'id'
    std::unordered_map<std::thread::id, std::stack<Transaction>> transaction_stacks;
    std::mutex mtx; 
    Logger& logger;

public:
    Database(Logger& logger);

    // thread id finder
    static std::thread::id currentThread();

    void recover();

    bool isTransactionActive();

    void set(const std::string& key, const std::string& value);

    std::string get(const std::string& key);

    void commit();

    // Begin a new transaction
    void begin();

    // Rollback to the last transaction
    void rollback();
};


#endif