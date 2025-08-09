#include "status.h"
#include "scenario.h"
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <sys/ioctl.h>
#include <unistd.h>

namespace status {

static std::atomic<double> gRate{0.0};
static std::atomic<uint64_t> gLatency{0};
static std::mutex printMutex;
static std::mutex rateMutex;
static std::deque<std::pair<std::chrono::steady_clock::time_point, uint64_t>>
    rateHistory;
static constexpr std::chrono::seconds RATE_WINDOW{1};

void updateRate(uint64_t count, std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(rateMutex);
    rateHistory.emplace_back(now, count);
    auto cutoff = now - RATE_WINDOW;
    while (!rateHistory.empty() && rateHistory.front().first < cutoff) {
        rateHistory.pop_front();
    }
    if (rateHistory.size() >= 2) {
        auto first = rateHistory.front();
        auto last = rateHistory.back();
        double dt =
            std::chrono::duration_cast<std::chrono::duration<double>>(last.first -
                                                                    first.first)
                .count();
        uint64_t dp = last.second - first.second;
        double r = dt > 0 ? static_cast<double>(dp) / dt : 0.0;
        gRate.store(r, std::memory_order_relaxed);
    } else {
        gRate.store(0.0, std::memory_order_relaxed);
    }
}

void updateLatency(uint64_t l) {
    gLatency.store(l, std::memory_order_relaxed);
}

void printStatus() {
    std::lock_guard<std::mutex> lock(printMutex);
    struct winsize w {};
    double rate = gRate.load(std::memory_order_relaxed);
    uint64_t latency = gLatency.load(std::memory_order_relaxed);
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row >= 4) {
        std::cout << "\0337"; // save cursor
        int start = w.ws_row - 3;
        std::cout << "\033[" << start << ";1H\033[2K" << "Press Ctrl+C to exit";
        std::cout << "\033[" << start + 1 << ";1H\033[2K";
        std::cout << "Current latency: " << latency << " us";
        std::cout << "\033[" << start + 2 << ";1H\033[2K";
        std::cout << "Current rate: " << rate << " events/s";
        std::cout << "\033[" << start + 3 << ";1H\033[2K";
        std::cout << "Current mode: " << modeToString(gScenario->mode);
        std::cout << "\0338";
    } else {
        std::cout << "Press Ctrl+C to exit\n";
        std::cout << "Current latency: " << latency << " us\n";
        std::cout << "Current rate: " << rate << " events/s\n";
        std::cout << "Current mode: " << modeToString(gScenario->mode) << std::endl;
    }
    std::cout.flush();
}

} // namespace status
