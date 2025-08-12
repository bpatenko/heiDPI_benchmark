#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <sys/select.h>
#include <sys/ioctl.h>

#include "config.h"
#include "generator.h"
#include "watcher.h"
#include "sample_queue.h"
#include "analyzer.h"
#include "logger_launcher.h"
#include "scenario.h"

std::atomic<bool> running(true);
std::atomic<bool> generatorReady(false);
std::atomic<bool> loggerStarted(false);

static void printManualMenu(const std::vector<ScenarioPtr>& presets) {
    struct winsize w {};
    size_t required = presets.size() + 2; // header + items + prompt
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row >= required + 5) {
        int row = 1;
        std::cout << "\033[" << row++ << ";1H\033[2KAvailable scenarios:";
        for (size_t i = 0; i < presets.size(); ++i) {
            std::cout << "\033[" << row++ << ";1H\033[2K  [" << i << "] "
                      << modeToString(presets[i]->mode);
        }
        std::cout << "\033[" << row << ";1H\033[2KSelect scenario index (or 'q' to quit): "
                  << std::flush;
    } else {
        std::cout << "Available scenarios:" << std::endl;
        for (size_t i = 0; i < presets.size(); ++i) {
            std::cout << "  [" << i << "] " << modeToString(presets[i]->mode) << std::endl;
        }
        std::cout << "Select scenario index (or 'q' to quit): " << std::flush;
    }
}

static void startSwitcher(const ScenarioFile& cfg, std::atomic<bool>& running) {
    using namespace std::chrono_literals;

    std::vector<ScenarioPtr> presets;
    for (const auto& sc : cfg.scenarios) {
        presets.push_back(std::make_shared<ScenarioConfig>(sc));
    }
    if (presets.empty()) {
        presets.push_back(std::make_shared<ScenarioConfig>());
    }

    size_t idx = 0;
    std::atomic_store_explicit(&gScenario, presets[idx], std::memory_order_release);
    std::cout << "[Switcher] Scenario #" << idx << " active" << std::endl;

    if (cfg.manual) {
        while (running.load()) {
            printManualMenu(presets);

            std::string line;
            bool gotLine = false;
            while (running.load() && !gotLine) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(STDIN_FILENO, &rfds);
                struct timeval tv {0, 100000};
                int ret = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
                if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
                    if (!std::getline(std::cin, line)) {
                        running = false;
                        break;
                    }
                    gotLine = true;
                } else if (ret < 0) {
                    running = false;
                    break;
                }
            }

            if (!running.load()) {
                break;
            }
            if (!gotLine) {
                continue;
            }

            if (line == "q" || line == "quit") {
                running = false;
                break;
            }
            try {
                size_t newIdx = std::stoul(line);
                if (newIdx < presets.size()) {
                    idx = newIdx;
                    std::atomic_store_explicit(&gScenario, presets[idx], std::memory_order_release);
                    std::cout << "[Switcher] Scenario #" << idx << " active" << std::endl;
                } else {
                    std::cout << "Invalid index" << std::endl;
                }
            } catch (...) {
                std::cout << "Invalid input" << std::endl;
            }
        }
    } else {
        while (running.load()) {
            for (int s = 0; s < cfg.interval_seconds && running.load(); ++s) {
                std::this_thread::sleep_for(1s);
            }
            idx = (idx + 1) % presets.size();
            std::atomic_store_explicit(&gScenario, presets[idx], std::memory_order_release);
            std::cout << "[Switcher] Scenario #" << idx << " active" << std::endl;
        }
    }
}

void signalHandler(int signum) {
    std::cout << "\nSignal " << signum << " received, stopping..." << std::endl;
    running = false;
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

    // 5. Run scenario switcher in main thread
    startSwitcher(scenarioFile, running);

    // 6. Cleanup
    genThread.join();
    watchThread.join();
    analyzerThread.join();

    kill(loggerPid, SIGTERM);
    std::cout << "Benchmark terminated." << std::endl;
    return 0;
}
