#include "Logger.h"

Logger::Logger(const std::string& filename)
    : filename(filename) {
    logFile.open(filename, std::ios::app);

    if (!logFile.is_open()) {
        throw std::runtime_error("Failed to open WAL file: " + filename);
    }

    output = &logFile;
}

Logger::Logger(const std::string& filename, std::ostream& outputOverride)
    : filename(filename), output(&outputOverride) {}

const std::string& Logger::getFilename() const {
    return filename;
}

void Logger::log(const std::string& key, const std::string& val) {
    std::lock_guard<std::mutex> lock(logMtx);

    if (output == nullptr) {
        throw std::runtime_error("WAL output is unavailable: " + filename);
    }

    *output << key << "," << val << '\n';
    output->flush();

    if (!*output) {
        throw std::runtime_error("Failed to write WAL file: " + filename);
    }
}

void Logger::logRecord(const std::string& record) {
    std::lock_guard<std::mutex> lock(logMtx);

    if (output == nullptr) {
        throw std::runtime_error("WAL output is unavailable: " + filename);
    }

    *output << record << '\n';
    output->flush();

    if (!*output) {
        throw std::runtime_error("Failed to write WAL file: " + filename);
    }
}


Logger::~Logger() {
    logFile.close();
}
