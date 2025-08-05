#ifndef WATCHER_H
#define WATCHER_H
#include <string>
#include <atomic>
#include <sys/types.h>
#include "sample_queue.h"

void startWatcher(const std::string& path,
                  SampleQueue& queue,
                  std::atomic<bool>& running,
                  pid_t loggerPid);
#endif // WATCHER_H