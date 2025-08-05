#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>

enum class Mode { IDLE, BURST, RAMP };

struct ScenarioConfig {
    Mode mode                 = Mode::IDLE;

    // ---- IDLE ----
    double idle_rate          = 100.0;  // pkts/s

    // ---- BURST ----
    double burst_rate         = 80'000.0;
    double idle_rate_burst    = 1'000.0;
    std::chrono::milliseconds burst_len {200};
    std::chrono::milliseconds idle_len  {800};

    // ---- RAMP ----
    double start_rate         = 500.0;
    double end_rate           = 20'000.0;
    std::chrono::seconds ramp_dur {10};
    std::chrono::seconds hold_dur {5};

    // ---- intern ---- (Sender füllt, Switcher lässt unangetastet)
    mutable uint64_t pkt_sent = 0;
    mutable uint64_t cycle_ns = 0;
};

using ScenarioPtr = std::shared_ptr<const ScenarioConfig>;

// globale, threadsichere Quelle der Wahrheit
extern ScenarioPtr gScenario;

// Timing-Funktion
std::chrono::microseconds nextInterval(const ScenarioConfig& cfg);

// Helper to convert a Mode enum to a human readable string
const char* modeToString(Mode mode);

// Convert string to Mode enum
Mode modeFromString(const std::string& name);

struct ScenarioFile {
    bool manual = false;             // manual selection or automatic
    int interval_seconds = 30;       // switch interval for automatic mode
    std::vector<ScenarioConfig> scenarios;
};

ScenarioFile loadScenarioFile(const std::string& path);
