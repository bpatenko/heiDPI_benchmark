#ifndef SAMPLE_QUEUE_H
#define SAMPLE_QUEUE_H

#include <cstdint>
#include <utility>
#include "readerwriterqueue/readerwriterqueue.h"

struct Sample {
    uint64_t packet_id{};
    uint64_t generator_ts{};
    uint64_t watcher_ts{};
};

using SampleQueue = moodycamel::ReaderWriterQueue<Sample>;

#endif // SAMPLE_QUEUE_H
