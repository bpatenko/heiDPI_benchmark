#include "Config.hpp"

Config::Config(const std::string &path) {
    YAML::Node config = YAML::LoadFile(path);
    auto logNode = config["logging"];
    if (logNode) {
        logging_cfg.level = logNode["level"].as<std::string>("INFO");
        logging_cfg.format = logNode["format"].as<std::string>("%Y-%m-%dT%H:%M:%S");
        logging_cfg.datefmt = logNode["datefmt"].as<std::string>("%Y-%m-%dT%H:%M:%S");
        if (logNode["filename"]) logging_cfg.filename = logNode["filename"].as<std::string>();
    }

    auto parseEvent = [](const YAML::Node &node, EventConfig &cfg) {
        if (!node) return;
        if (node["ignore_fields"]) cfg.ignore_fields = node["ignore_fields"].as<std::vector<std::string>>();
        if (node["ignore_risks"]) cfg.ignore_risks = node["ignore_risks"].as<std::vector<std::string>>();
        if (node["flow_event_name"]) cfg.event_names = node["flow_event_name"].as<std::vector<std::string>>();
        if (node["packet_event_name"]) cfg.event_names = node["packet_event_name"].as<std::vector<std::string>>();
        if (node["daemon_event_name"]) cfg.event_names = node["daemon_event_name"].as<std::vector<std::string>>();
        if (node["error_event_name"]) cfg.event_names = node["error_event_name"].as<std::vector<std::string>>();
        if (node["filename"]) cfg.filename = node["filename"].as<std::string>();
        if (node["threads"]) cfg.threads = node["threads"].as<int>();
        if (node["geoip2_city"]) {
            auto geo = node["geoip2_city"];
            cfg.geoip_enabled = geo["enabled"].as<bool>(false);
            if (geo["filepath"]) cfg.geoip_path = geo["filepath"].as<std::string>();
            if (geo["keys"]) cfg.geoip_keys = geo["keys"].as<std::vector<std::string>>();
        }
    };

    parseEvent(config["flow_event"], flow_cfg);
    parseEvent(config["packet_event"], packet_cfg);
    parseEvent(config["daemon_event"], daemon_cfg);
    parseEvent(config["error_event"], error_cfg);
}

