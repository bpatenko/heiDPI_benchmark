#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>

#include "config.h"
#include "generator.h"
#include "watcher.h"
#include "sample_queue.h"
#include "analyzer.h"
#include "logger_launcher.h"
#include "switcher.h"
#include "scenario.h"

std::atomic<bool> running(true);
std::atomic<bool> generatorReady(false);
std::atomic<bool> loggerStarted(false);

void signalHandler(int signum) {
    std::cout << "\nSignal " << signum << " received, stopping..." << std::endl;
    running = false;
}

void waitForStop() {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    Config config = loadConfig((argc > 1) ? argv[1] : "config.json");
    ScenarioFile scenarioFile = loadScenarioFile(config.scenarioPath);

    SampleQueue sampleQueue;

    // 1. Start generator thread (server socket)
    std::thread genThread(startGenerator,
                          config.generatorParams,
                          std::ref(running),
                          std::ref(generatorReady),
                          std::ref(loggerStarted));

    // 2. Wait until generator has bound its socket
    while (!generatorReady.load() && running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!running.load()) {
        genThread.join();
        return 0;
    }

    std::cout << "Generator is ready. Starting heiDPI_logger..." << std::endl;

    // 3. Launch heiDPI_logger after socket exists
    pid_t loggerPid = -1;
    if (config.straceEnabled) {
        if (config.loggerType == "binary") {
            loggerPid = launchBinaryLoggerStrace(
                config.loggerBinary,
                config.generatorParams.host,
                config.generatorParams.port,
                config.loggerConfigPath);
        } else {
            loggerPid = launchPythonLoggerStrace(
                config.loggerModule,
                config.generatorParams.host,
                config.generatorParams.port,
                config.loggerConfigPath);
        }
    } else {
        if (config.loggerType == "binary") {
            loggerPid = launchBinaryLogger(
                config.loggerBinary,
                config.generatorParams.host,
                config.generatorParams.port,
                config.loggerConfigPath);
        } else {
            loggerPid = launchPythonLogger(
                config.loggerModule,
                config.generatorParams.host,
                config.generatorParams.port,
                config.loggerConfigPath);
        }
    }
    loggerStarted = true;
    std::cout << "Started heiDPI_logger (PID: " << loggerPid << ")" << std::endl;

    // 4. Start watcher and analyzer threads
    std::thread watchThread(startWatcher,
                            config.outputFilePath,
                            std::ref(sampleQueue),
                            std::ref(running),
                            loggerPid);
    std::thread analyzerThread(startAnalyzer,
                               std::ref(sampleQueue),
                               std::ref(running));
    std::thread switchThread(startSwitcher, scenarioFile, std::ref(running));

    // 5. Wait for Ctrl+C
    waitForStop();

    // 6. Cleanup
    genThread.join();
    watchThread.join();
    analyzerThread.join();
    switchThread.join();

    kill(loggerPid, SIGTERM);
    std::cout << "Benchmark terminated." << std::endl;
    return 0;
}