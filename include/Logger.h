#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <fstream>

class Logger {
    std::ofstream logFile;                        // logFile is a file stream object (write only)
    std::mutex logMtx;
    
public:
    Logger(const std::string& filename);

    void log(const std::string& key, const std::string& val);

    ~Logger();
};

#endif