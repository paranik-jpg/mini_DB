#include "Logger.h"
#include "ThreadPool.h"
#include "Database.h"

#include <iostream>
#include <vector>
#include <string>

std::mutex coutMtx;
void safePrint(const std::string& msg) {
    std::lock_guard<std::mutex> lock(coutMtx);
    std::cout << msg << '\n';
}

int main() {
    Logger logger("db.log");
    Database mydb(logger);
    ThreadPool pool(std::thread::hardware_concurrency());

    // 1. Concurrent Independent Transactions with Commits & Rollbacks
    for(int i = 1; i <= 200; ++i) {
        pool.enqueue([&mydb, i]() {
            mydb.begin();
            mydb.set("user_" + std::to_string(i), std::to_string(i * 10));

            if (i % 2 == 0) {
                // Nested transaction verification
                mydb.begin();
                mydb.set("user_" + std::to_string(i), std::to_string(i * 100));
                mydb.rollback(); // Must revert back to (i * 10)

                mydb.commit();
                safePrint("Txn " + std::to_string(i) + " committed.");
            } else {
                mydb.rollback();
                safePrint("Txn " + std::to_string(i) + " rolled back.");
            }
        });
    }

    // 2. Concurrent Readers (Verifying optimistic non-blocking read isolation)
    for(int i = 1; i <= 50; ++i) {
        pool.enqueue([&mydb, i]() {
            auto val = mydb.get("user_" + std::to_string(i));
            safePrint("Read user_" + std::to_string(i) + " => " +
                      (val.has_value() ? *val : "<missing>"));
        });
    }

    return 0;
}
