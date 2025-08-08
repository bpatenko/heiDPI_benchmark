#pragma once
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

#include "Config.hpp"

/**
 * @brief Very small logger writing to stdout and optional file.
 */
class Logger {
public:
    enum class Level { ERROR = 0, INFO = 1 };

    static void init(const LoggingConfig &cfg);
    static void shutdown();
    static void info(const std::string &msg);
    static void error(const std::string &msg);

private:
    static void worker();

    static std::mutex queue_mtx;
    static std::condition_variable cv;
    static std::queue<std::pair<Level, std::string>> queue;
    static bool running;
    static std::thread thread;

    static std::mutex console_mtx;
    static std::mutex file_mtx;
    static std::ofstream file;
    static Level current_level;
};

