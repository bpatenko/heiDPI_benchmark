#ifndef GENERATOR_H
#define GENERATOR_H
#include <atomic>
#include "config.h"

void startGenerator(int clientSock,
                    std::atomic<bool>& running,
                    const EventProbabilities& probs);
#endif // GENERATOR_H
