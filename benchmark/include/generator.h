#ifndef GENERATOR_H
#define GENERATOR_H
#include "config.h"
#include <atomic>
void startGenerator(const GeneratorParams& params,
                    std::atomic<bool>& running,
                    std::atomic<bool>& ready,
                    std::atomic<bool>& startSending);
#endif // GENERATOR_H