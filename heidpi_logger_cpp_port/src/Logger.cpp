#include "Logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

std::mutex Logger::mtx;
std::ofstream Logger::file;

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&tt);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%FT%T", &tm);
    return std::string(buf);
}

void Logger::init(const LoggingConfig &cfg) {
    if (!cfg.filename.empty()) {
        file.open(cfg.filename, std::ios::app);
    }
}

void Logger::info(const std::string &msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string line = timestamp() + " INFO: " + msg + "\n";
    std::cout << line;
    if (file.is_open()) file << line;
}

void Logger::error(const std::string &msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string line = timestamp() + " ERROR: " + msg + "\n";
    std::cerr << line;
    if (file.is_open()) file << line;
}

