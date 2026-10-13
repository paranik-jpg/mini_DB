#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <cstdint>
#include <unordered_map>
#include <string>
#include <optional>

struct Transaction {
    // Unique ID used to associate WAL records.
    std::uint64_t id = 0;

    // Original values, saved for rollback.
    std::unordered_map<std::string, std::optional<std::string>> undo_log;

    // Values observed by reads, used for optimistic validation.
    std::unordered_map<std::string, std::optional<std::string>> read_set;

    // Latest pending values, used for commit.
    std::unordered_map<std::string, std::optional<std::string>> redo_log;
};

#endif
