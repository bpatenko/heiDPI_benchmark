// src/config.cpp
#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

Config loadConfig(const std::string& path) {
    Config cfg;

    // Versuche, die Datei zu öffnen
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "config.cpp: Could not open config file " << path
                  << ", falling back to defaults\n";
        // Default-Werte
        cfg.loggerType       = "python";
        cfg.loggerModule     = "heiDPI_logger";
        cfg.loggerBinary     = "heiDPI_logger.bin";
        cfg.loggerConfigPath = "config.yml";
        cfg.outputFilePath   = "flow_event.json";
        cfg.scenarioPath     = "scenarios.json";
        cfg.generatorParams  = {"127.0.0.1", 7000, 1.0, 128};
        return cfg;
    }

    // JSON einlesen
    json j;
    in >> j;

    // Paths
    cfg.loggerType       = j.value("loggerType", "python");
    cfg.loggerModule     = j.value("loggerModule", "heiDPI_logger");
    cfg.loggerBinary     = j.value("loggerBinary", "heiDPI_logger.bin");
    cfg.loggerConfigPath = j.value("loggerConfigPath", "config.yml");
    cfg.outputFilePath   = j.value("outputFilePath", "flow_event.json");
    cfg.scenarioPath     = j.value("scenarioPath", "scenarios.json");

    // Generator-Params
    auto gj = j["generatorParams"];
    cfg.generatorParams.host         = gj.value("host", "127.0.0.1");
    cfg.generatorParams.port         = gj.value("port", 7000);
    cfg.generatorParams.rate         = gj.value("rate", 1.0);
    cfg.generatorParams.message_size = gj.value("message_size", 128);

    return cfg;
}
