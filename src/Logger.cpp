#include "Logger.h"

Logger::Logger(const std::string& filename) {
    logFile.open(filename, std::ios::app);    // Open in append mode
}

void Logger::log(const std::string& key, const std::string& val) {
    std::lock_guard<std::mutex> lock(logMtx);  // Only one thread writes at a time
    logFile << key << "," << val << std::endl;
    logFile.flush();                           // Ensure it actually writes to disk!
}

void Logger::logRecord(const std::string& record) {
  std::lock_guard<std::mutex> lock(logMtx);

  logFile << record << '\n';
  logFile.flush();
}

Logger::~Logger() {
    logFile.close();
}
