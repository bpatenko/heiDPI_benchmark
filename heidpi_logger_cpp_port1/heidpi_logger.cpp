//
// Created by Bogdan Patenko on 29.07.25.
//

#include "heidpi_logger.h"
#include "thread_pool.h"

#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <future>
#include <yaml-cpp/yaml.h>
#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

void force_cli11_vtables() {
    CLI::Formatter f;
    CLI::ConfigBase c;
}

using json = nlohmann::json;

// Globale Variablen
bool SHOW_FLOW_EVENTS = false;
bool SHOW_PACKET_EVENTS = false;
bool SHOW_DAEMON_EVENTS = false;
bool SHOW_ERROR_EVENTS = false;

json FLOW_CONFIG, PACKET_CONFIG, DAEMON_CONFIG, ERROR_CONFIG, LOGGING_CONFIG;
std::unique_ptr<ThreadPool> POOL_FLOW, POOL_PACKET, POOL_DAEMON, POOL_ERROR;

const std::string LOGGING_DATEFMT = "%Y-%m-%d %H:%M:%S";
std::string JSON_PATH = "./logs";

// Hilfsfunktionen
std::string getEnvOrDefault(const std::string& key, const std::string& fallback) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : fallback;
}

std::string dir_path(const std::string& path) {
    if (std::filesystem::is_directory(path)) {
        return path;
    } else {
        throw std::runtime_error("Not a directory: " + path);
    }
}

std::string file_path(const std::string& path) {
    if (std::filesystem::is_regular_file(path)) {
        return path;
    } else {
        throw std::runtime_error("File not found: " + path);
    }
}

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);

    std::ostringstream oss;
    oss << std::put_time(&tm, LOGGING_DATEFMT.c_str());
    return oss.str();
}

void heidpi_log_event(
    const nlohmann::json& config_dict,
    const nlohmann::json& json_dict,
    const std::function<void(const nlohmann::json&, nlohmann::json&)>& additional_processing
) {
    // Deepcopy
    nlohmann::json json_dict_copy = json_dict;

    // Timestamp hinzufügen
    json_dict_copy["timestamp"] = get_timestamp();

    // Optionaler Callback
    if (additional_processing) {
        additional_processing(config_dict, json_dict_copy);
    }

    // ignore_fields entfernen
    if (config_dict.contains("ignore_fields") && config_dict["ignore_fields"].is_array()) {
        for (const auto& field : config_dict["ignore_fields"]) {
            if (field.is_string()) {
                json_dict_copy.erase(field.get<std::string>());
            }
        }
    }

    // Schreiben in Datei
    std::string filename = JSON_PATH + "/" + config_dict["filename"].get<std::string>() + ".json";
    std::ofstream out_file(filename, std::ios::app);
    if (out_file.is_open()) {
        out_file << json_dict_copy.dump() << "\n";
        out_file.close();
    } else {
        std::cerr << "Fehler beim Öffnen der Datei: " << filename << std::endl;
    }

    // Speicherbereinigung: nicht nötig, da RAII
}

void heidpi_flow_processing(const nlohmann::json& config_dict, nlohmann::json& json_dict) {
    if (config_dict.contains("geoip2_city") && config_dict["geoip2_city"]["enabled"].get<bool>()) {
        // 🔧 Platzhalter für GeoIP2-Integration
        std::cout << "[GeoIP2] Verarbeitung ist aktiviert, aber nicht implementiert." << std::endl;
        // Später kannst du hier z. B. geoip2_lookup(json_dict["src_ip"], json_dict["dst_ip"]); aufrufen
    }

    // flow_risk ignorieren
    if (json_dict.contains("ndpi") &&
    json_dict["ndpi"].contains("flow_risk") &&
    json_dict["ndpi"]["flow_risk"].is_object() &&
    config_dict.contains("ignore_risks") &&
    config_dict["ignore_risks"].is_array()) {

        auto& flow_risk = json_dict["ndpi"]["flow_risk"];
        std::set<std::string> ignore_keys;

        // Baue eine Menge aller ignorierten Keys als String auf
        for (const auto& risk : config_dict["ignore_risks"]) {
            if (risk.is_string()) {
                ignore_keys.insert(risk.get<std::string>());
            } else if (risk.is_number_integer()) {
                ignore_keys.insert(std::to_string(risk.get<int>()));
            }
        }

        // Alle Keys durchgehen, vergleichen als string, dann löschen
        std::vector<nlohmann::json::object_t::key_type> keys_to_delete;

        for (auto it = flow_risk.begin(); it != flow_risk.end(); ++it) {
            std::string key_str = it.key();
            if (ignore_keys.count(key_str)) {
                keys_to_delete.push_back(it.key());
            }
        }

        for (const auto& k : keys_to_delete) {
            flow_risk.erase(k);
        }
    }



}

void heidpi_worker(
    const std::string& address,
    const std::function<void(const json&)>& callback,
    const std::string& filter
) {
    nDPIsrvdSocket nsock;
    nsock.connectTo(address);

    auto wrapped_function = [callback](const json& j, std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*) -> bool {
        callback(j);
        return true;
    };

    nsock.loop(wrapped_function, nullptr, nullptr);

    if (!filter.empty()) {
        nsock.addFilter(filter);
    }
}

bool heidpi_process_packet_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> /* instance */,
    std::shared_ptr<Flow> /* current_flow */,
    void* /* global_user_data */
) {
    if (SHOW_PACKET_EVENTS && json_dict.contains("packet_event_id")) {
        if (json_dict.contains("packet_event_name") &&
            PACKET_CONFIG.contains("packet_event_name") &&
            PACKET_CONFIG["packet_event_name"].is_array()) {

            const std::string& event_name = json_dict["packet_event_name"];

            for (const auto& name : PACKET_CONFIG["packet_event_name"]) {
                if (name == event_name) {
                    POOL_PACKET->submit(heidpi_log_event, PACKET_CONFIG, json_dict, nullptr);
                    break;
                }
            }
            }
    }
    return true;
}


bool heidpi_process_flow_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> /* instance */,
    std::shared_ptr<Flow> /* current_flow */,
    void* /* global_user_data */
) {
    if (SHOW_FLOW_EVENTS && json_dict.contains("flow_event_id")) {
        if (json_dict.contains("flow_event_name") &&
            FLOW_CONFIG.contains("flow_event_name") &&
            FLOW_CONFIG["flow_event_name"].is_array()) {

            const std::string& event_name = json_dict["flow_event_name"];

            for (const auto& name : FLOW_CONFIG["flow_event_name"]) {
                if (name == event_name) {
                    POOL_FLOW->submit(
                        heidpi_log_event,
                    FLOW_CONFIG,
                        json_dict,
                        std::function<void(const nlohmann::json&, nlohmann::json&)>(heidpi_flow_processing)
                    );
                }

            }
            }
    }
    return true;
}

bool heidpi_process_daemon_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> /* instance */,
    std::shared_ptr<Flow> /* current_flow */,
    void* /* global_user_data */
) {
    if (SHOW_DAEMON_EVENTS && json_dict.contains("daemon_event_id")) {
        if (json_dict.contains("daemon_event_name") &&
            DAEMON_CONFIG.contains("daemon_event_name") &&
            DAEMON_CONFIG["daemon_event_name"].is_array()) {

            const std::string& event_name = json_dict["daemon_event_name"];

            for (const auto& name : DAEMON_CONFIG["daemon_event_name"]) {
                if (name == event_name) {
                    POOL_DAEMON->submit(heidpi_log_event, DAEMON_CONFIG, json_dict, nullptr);
                    break;
                }
            }
            }
    }
    return true;
}

bool heidpi_process_error_events(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> /* instance */,
    std::shared_ptr<Flow> /* current_flow */,
    void* /* global_user_data */
) {
    if (SHOW_ERROR_EVENTS && json_dict.contains("error_event_id")) {
        if (json_dict.contains("error_event_name") &&
            ERROR_CONFIG.contains("error_event_name") &&
            ERROR_CONFIG["error_event_name"].is_array()) {

            const std::string& event_name = json_dict["error_event_name"];

            for (const auto& name : ERROR_CONFIG["error_event_name"]) {
                if (name == event_name) {
                    POOL_ERROR->submit(heidpi_log_event, ERROR_CONFIG, json_dict, nullptr);
                    break;
                }
            }
            }
    }
    return true;
}

bool heidpi_type_analyzer(
    const nlohmann::json& json_dict,
    std::shared_ptr<Instance> /* instance */,
    std::shared_ptr<Flow> /* current_flow */,
    void* /* global_user_data */
) {
    if (SHOW_FLOW_EVENTS && json_dict.contains("flow_event_id")) {
        if (json_dict.contains("flow_event_name") &&
            FLOW_CONFIG["flow_event_name"].is_array()) {
            for (const auto& name : FLOW_CONFIG["flow_event_name"]) {
                if (name == json_dict["flow_event_name"]) {
                    POOL_FLOW->submit(
                        heidpi_log_event,
                    FLOW_CONFIG,
                        json_dict,
                        std::function<void(const nlohmann::json&, nlohmann::json&)>(heidpi_flow_processing)
                    );
                }
            }
        }
    } else if (SHOW_PACKET_EVENTS && json_dict.contains("packet_event_id")) {
        if (json_dict.contains("packet_event_name") &&
            PACKET_CONFIG["packet_event_name"].is_array()) {
            for (const auto& name : PACKET_CONFIG["packet_event_name"]) {
                if (name == json_dict["packet_event_name"]) {
                    POOL_PACKET->submit(heidpi_log_event, PACKET_CONFIG, json_dict, nullptr);
                    return true;
                }
            }
        }
    } else if (SHOW_DAEMON_EVENTS && json_dict.contains("daemon_event_id")) {
        if (json_dict.contains("daemon_event_name") &&
            DAEMON_CONFIG["daemon_event_name"].is_array()) {
            for (const auto& name : DAEMON_CONFIG["daemon_event_name"]) {
                if (name == json_dict["daemon_event_name"]) {
                    POOL_DAEMON->submit(heidpi_log_event, DAEMON_CONFIG, json_dict, nullptr);
                    return true;
                }
            }
        }
    } else if (SHOW_ERROR_EVENTS && json_dict.contains("error_event_id")) {
        if (json_dict.contains("error_event_name") &&
            ERROR_CONFIG["error_event_name"].is_array()) {
            for (const auto& name : ERROR_CONFIG["error_event_name"]) {
                if (name == json_dict["error_event_name"]) {
                    POOL_ERROR->submit(heidpi_log_event, ERROR_CONFIG, json_dict, nullptr);
                    return true;
                }
            }
        }
    }

    return true;
}



std::string heidpi_validateAddress(const ParsedArgs& args) {
    bool tcp_addr_set = false;
    std::string address;

    std::string host = args.host.empty() ? DEFAULT_HOST : args.host;
    int port = args.port;
    std::string unix_socket = args.unix_socket.empty() ? DEFAULT_UNIX : args.unix_socket;

    if (!args.host.empty()) {
        tcp_addr_set = true;
    }

    // Prüfen, ob Unix-Socket-Datei existiert und ein Socket ist
    struct stat statbuf {};
    int possible_sock_mode = 0;
    if (stat(unix_socket.c_str(), &statbuf) == 0) {
        possible_sock_mode = statbuf.st_mode;
    }

    if (!tcp_addr_set && S_ISSOCK(possible_sock_mode)) {
        address = unix_socket;
    } else {
        address = host + ":" + std::to_string(port);
    }

    return address;
}

nlohmann::json yaml_to_json(const YAML::Node& node) {
    using json = nlohmann::json;

    switch (node.Type()) {
        case YAML::NodeType::Null:
            return nullptr;
        case YAML::NodeType::Scalar:
            return node.as<std::string>();
        case YAML::NodeType::Sequence: {
            json j = json::array();
            for (const auto& item : node) {
                j.push_back(yaml_to_json(item));
            }
            return j;
        }
        case YAML::NodeType::Map: {
            json j;
            for (const auto& kv : node) {
                j[kv.first.as<std::string>()] = yaml_to_json(kv.second);
            }
            return j;
        }
        default:
            throw std::runtime_error("Unsupported YAML node type");
    }
}

void run_process_flow(const json& j) {
    heidpi_process_flow_events(j, nullptr, nullptr, nullptr);
}

void run_process_packet(const json& j) {
    heidpi_process_packet_events(j, nullptr, nullptr, nullptr);
}

void run_process_daemon(const json& j) {
    heidpi_process_daemon_events(j, nullptr, nullptr, nullptr);
}

void run_process_error(const json& j) {
    heidpi_process_error_events(j, nullptr, nullptr, nullptr);
}

int main(int argc, char* argv[]) {
    CLI::App app{"heiDPI Logger"};
    CLI::Formatter formatter;
    CLI::ConfigBase _dummy_config_for_vtable;

    std::string host, unix_socket;
    int port = 7000;
    std::string config_path = getEnvOrDefault("CONFIG", "./config.yml");
    std::string write_path  = getEnvOrDefault("WRITE", "./logs");
    std::string filter      = getEnvOrDefault("FILTER", "");

    bool show_daemon = false, show_packet = false, show_error = false, show_flow = false;

    app.add_option("--host", host, "Host");
    app.add_option("--unix", unix_socket, "Unix-Socket");
    app.add_option("--port", port, "Port")->default_val(port);
    app.add_option("--write", write_path, "Log-Pfad")->default_val(write_path);
    app.add_option("--config", config_path, "Config")->default_val(config_path);
    app.add_option("--filter", filter, "Filter")->default_val(filter);
    app.add_flag("--show-daemon-events", show_daemon);
    app.add_flag("--show-packet-events", show_packet);
    app.add_flag("--show-error-events", show_error);
    app.add_flag("--show-flow-events", show_flow);

    CLI11_PARSE(app, argc, argv);

    if (host.empty() && unix_socket.empty()) {
        std::cerr << "Bitte --host oder --unix angeben.\n";
        return 1;
    }
    std::string address = !unix_socket.empty() ? unix_socket : (host + ":" + std::to_string(port));

    YAML::Node yaml_config = YAML::LoadFile(config_path);
    json config = yaml_to_json(yaml_config);

    SHOW_PACKET_EVENTS = show_packet;
    SHOW_FLOW_EVENTS   = show_flow;
    SHOW_DAEMON_EVENTS = show_daemon;
    SHOW_ERROR_EVENTS  = show_error;
    JSON_PATH          = write_path;

    FLOW_CONFIG    = config["flow_event"];
    PACKET_CONFIG  = config["packet_event"];
    DAEMON_CONFIG  = config["daemon_event"];
    ERROR_CONFIG   = config["error_event"];
    LOGGING_CONFIG = config["logging"];

    POOL_FLOW   = std::make_unique<ThreadPool>(FLOW_CONFIG["threads"]);
    POOL_PACKET = std::make_unique<ThreadPool>(PACKET_CONFIG["threads"]);
    POOL_DAEMON = std::make_unique<ThreadPool>(DAEMON_CONFIG["threads"]);
    POOL_ERROR  = std::make_unique<ThreadPool>(ERROR_CONFIG["threads"]);

    std::cout << "Verbunden mit: " << address << std::endl;

    if (SHOW_FLOW_EVENTS)
        std::thread([=] { heidpi_worker(address, std::function<void(const json&)>(run_process_flow), filter); }).detach();
    if (SHOW_PACKET_EVENTS)
        std::thread([=] { heidpi_worker(address, std::function<void(const json&)>(run_process_packet), filter); }).detach();
    if (SHOW_DAEMON_EVENTS)
        std::thread([=] { heidpi_worker(address, std::function<void(const json&)>(run_process_daemon), filter); }).detach();
    if (SHOW_ERROR_EVENTS)
        std::thread([=] { heidpi_worker(address, std::function<void(const json&)>(run_process_error), filter); }).detach();

    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
