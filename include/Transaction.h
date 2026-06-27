#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <unordered_map>
#include <string>

struct Transaction {
    std::unordered_map<std::string, std::string> undo_log;
};

#endif