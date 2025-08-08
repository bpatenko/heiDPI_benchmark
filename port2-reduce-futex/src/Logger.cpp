#include "Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <cstdlib>

std::mutex Logger::queue_mtx;
std::condition_variable Logger::cv;
std::queue<std::pair<Logger::Level, std::string>> Logger::queue;
bool Logger::running = false;
std::thread Logger::thread;

std::mutex Logger::console_mtx;
std::mutex Logger::file_mtx;
std::ofstream Logger::file;
Logger::Level Logger::current_level = Logger::Level::INFO;

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&tt);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%FT%T", &tm);
    return std::string(buf);
}

void Logger::init(const LoggingConfig &cfg) {
    current_level = (cfg.level == "ERROR") ? Level::ERROR : Level::INFO;
    if (!cfg.filename.empty()) {
        file.open(cfg.filename, std::ios::app);
    }
    running = true;
    thread = std::thread(worker);
    std::atexit(Logger::shutdown);
}

void Logger::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        running = false;
    }
    cv.notify_all();
    if (thread.joinable()) thread.join();
    if (file.is_open()) {
        std::lock_guard<std::mutex> lk(file_mtx);
        file.close();
    }
}

void Logger::info(const std::string &msg) {
    if (current_level < Level::INFO) return;
    std::string line = timestamp() + " INFO: " + msg + "\n";
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        queue.emplace(Level::INFO, std::move(line));
    }
    cv.notify_one();
}

void Logger::error(const std::string &msg) {
    std::string line = timestamp() + " ERROR: " + msg + "\n";
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        queue.emplace(Level::ERROR, std::move(line));
    }
    cv.notify_one();
}

void Logger::worker() {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mtx);
        cv.wait(lock, [] { return !queue.empty() || !running; });
        if (queue.empty() && !running) break;
        auto item = std::move(queue.front());
        queue.pop();
        lock.unlock();

        {
            std::lock_guard<std::mutex> lk(console_mtx);
            if (item.first == Level::ERROR)
                std::cerr << item.second;
            else
                std::cout << item.second;
        }
        if (file.is_open()) {
            std::lock_guard<std::mutex> lk(file_mtx);
            file << item.second;
        }
    }
}

