#include "status.h"
#include "scenario.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <sys/ioctl.h>
#include <unistd.h>

namespace status {

static std::atomic<double> gRate{0.0};
static std::atomic<uint64_t> gLatency{0};
static std::mutex printMutex;
static std::mutex rateMutex;
static std::chrono::steady_clock::time_point lastTime;
static uint64_t lastCount = 0;
static bool hasLast = false;
static constexpr double ALPHA = 0.2; // EWMA smoothing factor

void updateRate(uint64_t count, std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(rateMutex);
    if (!hasLast) {
        lastTime = now;
        lastCount = count;
        hasLast = true;
        return;
    }
    double dt =
        std::chrono::duration_cast<std::chrono::duration<double>>(now - lastTime)
            .count();
    uint64_t dp = count - lastCount;
    double inst = dt > 0 ? static_cast<double>(dp) / dt : 0.0;
    double current = gRate.load(std::memory_order_relaxed);
    double smooth = ALPHA * inst + (1.0 - ALPHA) * current;
    gRate.store(smooth, std::memory_order_relaxed);
    lastTime = now;
    lastCount = count;
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
