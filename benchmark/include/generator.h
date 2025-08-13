#ifndef GENERATOR_H
#define GENERATOR_H
#include <atomic>
void startGenerator(int clientSock, std::atomic<bool>& running);
#endif // GENERATOR_H