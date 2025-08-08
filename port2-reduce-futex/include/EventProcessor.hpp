#pragma once
#include <string>
#include "Config.hpp"
#include "GeoIP.hpp"
#include "Logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>


/**
 * @brief Processes events based on configuration and writes them as JSON lines.
 */
class EventProcessor {
public:
    EventProcessor(const EventConfig &cfg, const std::string &outDir);
    ~EventProcessor();

    void process(const nlohmann::json &j);
private:
    EventConfig config;
    std::string directory;
    GeoIP geo;
    std::ofstream ofs;

};

