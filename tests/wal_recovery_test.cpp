#include "Database.h"
#include "Logger.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <fstream>

namespace {
constexpr const char* WAL = "wal_recovery_test.log";

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}
}

int main() {
    std::remove(WAL);

    const std::string specialKey = "key,with\nnewline";
    const std::string specialValue = "value,with\nnewline";
    bool passed = true;

    // Write committed data and leave one transaction uncommitted.
    {
        Logger logger(WAL);
        Database db(logger);

        db.set(specialKey, specialValue);
        db.set("empty-value", "");
        db.set("to-delete", "temporary");
        db.erase("to-delete");

        db.begin();
        db.set("committed-transaction", "survives restart");
        passed = check(db.commit(),
                       "transaction commits before restart") && passed;

        db.begin();
        db.set("uncommitted-transaction", "must not appear");
        // Intentionally do not commit this transaction.
    }

    // A new Database instance must reconstruct state from the same WAL.
    {
        Logger logger(WAL);
        Database recovered(logger);

        passed = check(
            recovered.get(specialKey) == specialValue,
            "comma/newline key and value survive recovery") && passed;

        passed = check(
            recovered.get("empty-value").empty(),
            "empty value survives recovery") && passed;

        passed = check(
            recovered.get("to-delete") == "NULL",
            "committed deletion survives recovery") && passed;

        passed = check(
            recovered.get("committed-transaction") == "survives restart",
            "committed transaction survives recovery") && passed;

        passed = check(
            recovered.get("uncommitted-transaction") == "NULL",
            "uncommitted transaction is not recovered") && passed;
    }

    // A final COMMIT record without a terminating newline is incomplete.
    {
        std::ofstream out(WAL, std::ios::trunc);
        out << "BEGIN,900\n";
        out << "SET2,900,74657374,76616C7565\n";
        out << "COMMIT,900"; // Deliberately no trailing newline.
    }

    {
        Logger logger(WAL);
        Database recovered(logger);

        passed = check(
            recovered.get("test") == "NULL",
            "unterminated final COMMIT is ignored") && passed;
    }

    std::remove(WAL);

    if (!passed) {
        return 1;
    }

    std::cout << "PASS: WAL recovery reconstructs committed state only\n";
    return 0;
}
