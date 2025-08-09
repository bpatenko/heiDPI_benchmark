#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace status {

void updateRate(uint64_t count, std::chrono::steady_clock::time_point now);
void updateLatency(uint64_t l);
void printStatus();

}
