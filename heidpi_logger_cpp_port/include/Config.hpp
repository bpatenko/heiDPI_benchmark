#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

/**
 * @brief Loads application configuration from a YAML file.
 */
struct LoggingConfig {
    std::string level{"INFO"};
    std::string format{"%Y-%m-%dT%H:%M:%S"};
    std::string datefmt{"%Y-%m-%dT%H:%M:%S"};
    std::string filename{}; // optional log file
};

struct EventConfig {
    std::vector<std::string> ignore_fields;
    std::vector<std::string> ignore_risks;
    std::vector<std::string> event_names; // empty -> allow all event names
    std::string filename{"event"};
    int threads{1};
    // GeoIP configuration (flow events only)
    bool geoip_enabled{false};
    std::string geoip_path{};
    std::vector<std::string> geoip_keys;
};

class Config {
public:
    explicit Config(const std::string &path);
    const LoggingConfig &logging() const { return logging_cfg; }
    const EventConfig &flowEvent() const { return flow_cfg; }
    const EventConfig &packetEvent() const { return packet_cfg; }
    const EventConfig &daemonEvent() const { return daemon_cfg; }
    const EventConfig &errorEvent() const { return error_cfg; }
private:
    LoggingConfig logging_cfg;
    EventConfig flow_cfg;
    EventConfig packet_cfg;
    EventConfig daemon_cfg;
    EventConfig error_cfg;
};

