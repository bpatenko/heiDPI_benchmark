#pragma once
#include <atomic>
#include <cstdint>

namespace status {

void updateRate(double r);
void updateLatency(uint64_t l);
void printStatus();

}
