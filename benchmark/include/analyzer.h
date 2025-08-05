#ifndef ANALYZER_H
#define ANALYZER_H
#include <atomic>
#include "sample_queue.h"

void startAnalyzer(SampleQueue& queue, std::atomic<bool>& running);
#endif // ANALYZER_H