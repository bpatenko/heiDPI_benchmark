#include "scenario.h"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

// Set default scenario with higher base rate to stress the generator
static ScenarioConfig makeDefaults() {
    ScenarioConfig cfg{};
    cfg.idle_rate = 10'000; // pkts/s
    return cfg;
}

static const ScenarioConfig defaults = makeDefaults();
ScenarioPtr gScenario{std::make_shared<ScenarioConfig>(defaults)};

using us = std::chrono::microseconds;

const char* modeToString(Mode m)
{
    switch (m) {
    case Mode::IDLE:
        return "IDLE";
    case Mode::BURST:
        return "BURST";
    case Mode::RAMP:
        return "RAMP";
    }
    return "UNKNOWN";
}

Mode modeFromString(const std::string& s)
{
    if (s == "IDLE" || s == "idle")
        return Mode::IDLE;
    if (s == "BURST" || s == "burst")
        return Mode::BURST;
    if (s == "RAMP" || s == "ramp")
        return Mode::RAMP;
    return Mode::IDLE;
}

std::chrono::microseconds nextInterval(const ScenarioConfig& c)
{
    switch (c.mode) {
    case Mode::IDLE:
        return us(static_cast<uint64_t>(1'000'000.0 / c.idle_rate));

    case Mode::BURST:
    return us(static_cast<uint64_t>(1'000'000.0 / c.burst_rate));


    case Mode::RAMP: {
        uint64_t now_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        if (c.cycle_ns == 0)
            const_cast<uint64_t&>(c.cycle_ns) = now_ns;

        double t = (now_ns - c.cycle_ns) / 1e9; // Sek.
        double rate =
            (t <= c.ramp_dur.count())
                ? c.start_rate +
                  (c.end_rate - c.start_rate) * (t / c.ramp_dur.count())
                : c.end_rate;
        ++c.pkt_sent;
        return us(static_cast<uint64_t>(1'000'000.0 / rate));
    }
    }
    return us(1'000'000); // fallback
}

using json = nlohmann::json;

ScenarioFile loadScenarioFile(const std::string& path)
{
    ScenarioFile file;
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "Scenario: could not open " << path
                  << ", using defaults\n";
        ScenarioConfig idle;
        idle.mode = Mode::IDLE;
        idle.idle_rate = 10'000;

        ScenarioConfig burst = idle;
        burst.mode = Mode::BURST;
        burst.burst_rate = 75'000;
        burst.idle_rate_burst = 1'000;
        burst.burst_len = std::chrono::milliseconds(250);
        burst.idle_len = std::chrono::milliseconds(750);

        ScenarioConfig ramp = idle;
        ramp.mode = Mode::RAMP;
        ramp.start_rate = 500;
        ramp.end_rate = 20'000;
        ramp.ramp_dur = std::chrono::seconds(15);

        file.manual = false;
        file.interval_seconds = 30;
        file.scenarios = {idle, burst, ramp};
        return file;
    }

    json j;
    in >> j;
    std::string modeStr = j.value("mode", "automatic");
    file.manual = (modeStr == "manual");
    file.interval_seconds = j.value("interval", 30);
    file.start_index = j.value("start_index", 0);
    file.kill_after = std::chrono::seconds(j.value("kill_after", 0));


    if (j.contains("scenarios") && j["scenarios"].is_array()) {
        for (const auto& sj : j["scenarios"]) {
            ScenarioConfig cfg;
            cfg.mode = modeFromString(sj.value("mode", "IDLE"));
            cfg.idle_rate = sj.value("idle_rate", cfg.idle_rate);
            cfg.burst_rate = sj.value("burst_rate", cfg.burst_rate);
            cfg.idle_rate_burst = sj.value("idle_rate_burst", cfg.idle_rate_burst);
            cfg.burst_len = std::chrono::milliseconds(
                sj.value("burst_len", static_cast<int>(cfg.burst_len.count())));
            cfg.idle_len = std::chrono::milliseconds(
                sj.value("idle_len", static_cast<int>(cfg.idle_len.count())));
            cfg.start_rate = sj.value("start_rate", cfg.start_rate);
            cfg.end_rate = sj.value("end_rate", cfg.end_rate);
            cfg.ramp_dur = std::chrono::seconds(
                sj.value("ramp_dur", static_cast<int>(cfg.ramp_dur.count())));
            cfg.hold_dur = std::chrono::seconds(
                sj.value("hold_dur", static_cast<int>(cfg.hold_dur.count())));
            file.scenarios.push_back(cfg);
        }
    }
    if (file.scenarios.empty()) {
        file.scenarios.push_back(ScenarioConfig{});
    }
    return file;
}
