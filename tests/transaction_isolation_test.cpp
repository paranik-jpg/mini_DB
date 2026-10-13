
#include "Database.h"
#include "Logger.h"

#include <cstdio>
#include <future>
#include <iostream>
#include <string>
#include <thread>

int main() {
    constexpr const char* WAL = "transaction_isolation_test.log";
    std::remove(WAL);

    bool passed = false;

    {
        Logger logger(WAL);
        Database db(logger);

        // Two records initially indicate that both resources are available.
        db.set("x", "1");
        db.set("y", "1");

        std::promise<void> firstReadDone;
        auto firstReadFuture = firstReadDone.get_future();

        std::promise<void> secondCommitDone;
        auto secondCommitFuture = secondCommitDone.get_future();

        bool firstCommit = true;
        bool secondCommit = false;
        bool secondSawX = false;

        std::thread t1([&] {
            db.begin();
            const auto y = db.get("y");
            db.set("x", "0");

            if (y != "1") {
                firstCommit = false;
                firstReadDone.set_value();
                return;
            }

            firstReadDone.set_value();
            secondCommitFuture.wait();

            // Must fail: y changed after this transaction read it.
            firstCommit = db.commit();
        });

        std::thread t2([&] {
            firstReadFuture.wait();

            db.begin();
            const auto x = db.get("x");
            secondSawX = (x == "1");
            db.set("y", "0");

            secondCommit = db.commit();
            secondCommitDone.set_value();
        });

        t1.join();
        t2.join();

        passed = secondSawX && secondCommit && !firstCommit
                 && db.get("x") == "1"
                 && db.get("y") == "0";
    }

    // Nested commit merges into parent; nested rollback discards child writes.
    {
        Logger logger(WAL);
        Database db(logger);

        db.set("nested", "original");

        db.begin();                 // Parent
        db.set("nested", "parent");

        db.begin();                 // Child
        db.set("nested", "child");
        if (!db.commit()) {         // Merge child into parent
            std::cerr << "FAIL: nested commit\n";
            return 1;
        }

        db.begin();                 // Another child
        db.set("nested", "discard");
        db.rollback();              // Discard this child

        if (db.get("nested") != "child") {
            std::cerr << "FAIL: nested rollback/visibility\n";
            return 1;
        }

        if (!db.commit() || db.get("nested") != "child") {
            std::cerr << "FAIL: outer commit after nested operations\n";
            return 1;
        }
    }

    std::remove(WAL);

    if (!passed) {
        std::cerr << "FAIL: stale-read transaction was not rejected\n";
        return 1;
    }

    std::cout << "PASS: optimistic read-set validation\n";
    return 0;
}
