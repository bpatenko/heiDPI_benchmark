#include "EventProcessor.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <filesystem>

EventProcessor::EventProcessor(const EventConfig &cfg, const std::string &outDir)
    : config(cfg), directory(outDir) {
    if (cfg.geoip_enabled && !cfg.geoip_path.empty()) {
        geo = std::make_unique<GeoIP>(cfg.geoip_path, cfg.geoip_keys);
    } else {
        // optional, aber hilfreich zur Diagnose:
        Logger::info(std::string("GeoIP disabled for '") + cfg.filename +
                     "' (enabled=" + (cfg.geoip_enabled ? "true" : "false") +
                     ", path=" + (cfg.geoip_path.empty() ? "<empty>" : cfg.geoip_path) + ")");
    }
}

static std::string nowTs() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&tt);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%FT%T", &tm);
    return std::string(buf);
}

void EventProcessor::process(const nlohmann::json &j) {
    nlohmann::json out = j;
    out["timestamp"] = nowTs();

    if (geo) { // statt config.geoip_enabled
        std::string src = j.value("src_ip", "");
        std::string dst = j.value("dst_ip", "");
        geo->enrich(src, dst, out);
    }
    for (const auto &field : config.ignore_fields) {
        out.erase(field);
    }
    if (!config.ignore_risks.empty() && out.contains("ndpi") && out["ndpi"].contains("flow_risk")) {
        for (const auto &risk : config.ignore_risks) {
            out["ndpi"]["flow_risk"].erase(risk);
        }
    }
    std::filesystem::create_directories(directory);
    auto path = std::filesystem::path(directory) / (config.filename + ".json");
    std::ofstream ofs(path, std::ios::app);
    if (!ofs.is_open()) {
        Logger::error("Failed to open output file: " + path.string());
        return;
    }
    ofs << out.dump() << std::endl;
}

