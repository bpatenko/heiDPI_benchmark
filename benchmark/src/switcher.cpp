#include "switcher.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

using namespace std::chrono_literals;

void startSwitcher(const ScenarioFile& cfg, std::atomic<bool>& running)
{
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
