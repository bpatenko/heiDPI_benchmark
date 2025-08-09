#include "analyzer.h"
#include "scenario.h"
#include "status.h"
#include <chrono>
#include <iostream>
#include <thread>

void startAnalyzer(SampleQueue& queue, std::atomic<bool>& running) {
    uint64_t currentLatency = 0;
    status::updateLatency(currentLatency);

    while (running.load()) {
        bool processed = false;
        Sample sample;
        while (queue.try_dequeue(sample)) {
            uint64_t latency = 0;
            if (sample.watcher_ts >= sample.generator_ts) {
                latency = sample.watcher_ts - sample.generator_ts;
            }
            currentLatency = latency;
            status::updateLatency(currentLatency);
            processed = true;
        }

        if (!processed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Drain remaining samples
    Sample sample;
    while (queue.try_dequeue(sample)) {
        uint64_t latency = 0;
        if (sample.watcher_ts >= sample.generator_ts) {
            latency = sample.watcher_ts - sample.generator_ts;
        }
        currentLatency = latency;
        status::updateLatency(currentLatency);
    }
    status::updateLatency(currentLatency);
}
