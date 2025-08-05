#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <nlohmann/json.hpp> // https://github.com/nlohmann/json
#include "heidpi_srvd.h"

using json = nlohmann::json;

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
    // Aktuelle Zeit als time_t
    auto now = std::chrono::system_clock::now();
    std::time_t time_now = std::chrono::system_clock::to_time_t(now);

    // Umwandlung in lokale Zeit
    std::tm local_time = *std::localtime(&time_now);

    // Formatieren des Zeitstempels
    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S"); // Entspricht z. B. LOGGING_CONFIG["datefmt"]
    return oss.str();
}



// Annahme: LOGGING_CONFIG und JSON_PATH sind global oder anderweitig verfügbar
extern const std::string JSON_PATH;

// Typdefinitionen zur Klarheit
using json = nlohmann::json;
using ConfigDict = std::unordered_map<std::string, json>;
using ProcessingFunction = std::function<void(const ConfigDict&, json&)>;

void heidpi_log_event(const ConfigDict& config_dict, const json& json_dict, ProcessingFunction additional_processing) {
    // Tiefe Kopie
    json json_dict_copy = json_dict;

    // Zeitstempel hinzufügen
    json_dict_copy["timestamp"] = get_timestamp();

    // Optionale Verarbeitung
    if (additional_processing) {
        additional_processing(config_dict, json_dict_copy);
    }

    // Felder ignorieren
    if (config_dict.contains("ignore_fields")) {
        for (const auto& field : config_dict.at("ignore_fields")) {
            json_dict_copy.erase(field.get<std::string>());
        }
    }

    // In Datei schreiben
    std::string filename = JSON_PATH + "/" + config_dict.at("filename").get<std::string>() + ".json";
    std::ofstream file(filename, std::ios::app);
    if (file.is_open()) {
        file << json_dict_copy.dump() << "\n";
        file.close();
    }
}

// Hilfsfunktion für verschachtelte Keys in JSON (z. B. "location.city.name")
std::optional<json> get_nested_value(const json& data, const std::string& key_path) {
    const json* current = &data;
    std::istringstream stream(key_path);
    std::string token;
    while (std::getline(stream, token, '.')) {
        if (current->contains(token)) {
            current = &(*current)[token];
        } else {
            return std::nullopt;
        }
    }
    return *current;
}

void heidpi_flow_processing(json& config_dict, json& json_dict) {
    if (config_dict["geoip2_city"]["enabled"].get<bool>()) {
        // GeoIP ausgeschaltet – Platzhalter
        /* do nothing */
    }

    if (json_dict.contains("ndpi") &&
        json_dict["ndpi"].contains("flow_risk") &&
        !config_dict["ignore_risks"].empty()) {

        for (const auto& risk : config_dict["ignore_risks"]) {
            json_dict["ndpi"]["flow_risk"].erase(risk.get<std::string>());
        }
        }
}




int main() {
}