
#include "Logger.h"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        Logger logger("/dev/full");

        bool writeFailed = false;
        try {
            logger.logRecord("TEST_RECORD");
        } catch (const std::runtime_error&) {
            writeFailed = true;
        }

        if (!writeFailed) {
            std::cerr << "FAIL: Logger did not report the write failure\n";
            return 1;
        }

        std::cout << "PASS: Logger reports WAL write/flush failure\n";
        return 0;
    } catch (const std::runtime_error& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
