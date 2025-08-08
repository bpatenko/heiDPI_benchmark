#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include "Config.hpp"

/**
 * @brief Very small logger writing to stdout and optional file.
 */
class Logger {
public:
    static void init(const LoggingConfig &cfg);
    static void info(const std::string &msg);
    static void error(const std::string &msg);
private:
    static std::mutex mtx;
    static std::ofstream file;
};

