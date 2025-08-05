//
// Created by Bogdan Patenko on 29.07.25.
//

#ifndef HEIDPI_CPP_HEIDPI_LOGGER_H
#define HEIDPI_CPP_HEIDPI_LOGGER_H

#include <string>
#include <yaml-cpp/yaml.h>
#include <fstream>

// Forward-Declarations oder Includes für heidpi-Komponenten
#include "heidpi_srvd.h"
#include "heidpi_env.h"

#include <nlohmann/json.hpp>
#include <functional>
#include <sys/stat.h>
#include <string>

// Globale Konstanten
inline const std::string DEFAULT_HOST = "127.0.0.1";
inline const int DEFAULT_PORT = 7000;
inline const std::string DEFAULT_UNIX = "/tmp/heidpi.sock";

extern const std::string LOGGING_DATEFMT;

extern std::string JSON_PATH;

std::string dir_path(const std::string& path);

std::string file_path(const std::string& path);

std::string get_timestamp();




void heidpi_log_event(
    const nlohmann::json& config_dict,
    const nlohmann::json& json_dict,
    const std::function<void(const nlohmann::json&, nlohmann::json&)>& additional_processing = nullptr
);

void heidpi_flow_processing(const nlohmann::json& config_dict, nlohmann::json& json_dict);

void heidpi_worker(
    const std::string& address,
    const std::function<void(nlohmann::json&)>& function,
    const std::string& filter
);

bool heidpi_process_packet_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> instance,
    std::shared_ptr<Flow> current_flow,
    void* global_user_data
);

bool heidpi_process_flow_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> instance,
    std::shared_ptr<Flow> current_flow,
    void* global_user_data
);

bool heidpi_process_daemon_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> instance,
    std::shared_ptr<Flow> current_flow,
    void* global_user_data
);

bool heidpi_process_error_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> instance,
    std::shared_ptr<Flow> current_flow,
    void* global_user_data
);

bool heidpi_type_analyzer(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> instance,
    std::shared_ptr<Flow> current_flow,
    void* global_user_data
);

struct ParsedArgs {
    std::string host;
    std::string unix_socket;
    int port;
};

std::string heidpi_validateAddress(const ParsedArgs& args);

#endif //HEIDPI_CPP_HEIDPI_LOGGER_H