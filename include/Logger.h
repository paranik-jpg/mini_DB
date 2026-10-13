
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <fstream>
#include <stdexcept>
#include <ostream>

class Logger {
    std::string filename;
    std::ofstream logFile;
    std::mutex logMtx;
    std::ostream* output = nullptr;

public:
    // Existing constructor remains unchanged.
    Logger(const std::string& filename);

    // Allows tests to inject a failing output stream.
    Logger(const std::string& filename, std::ostream& outputOverride);

    const std::string& getFilename() const;

    void log(const std::string& key, const std::string& val);
    void logRecord(const std::string& record);

    ~Logger();
};

#endif
