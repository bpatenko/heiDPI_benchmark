//
// Created by Bogdan Patenko on 29.07.25.
//

#ifndef HEIDPI_CPP_HEIDPI_SRVD_H
#define HEIDPI_CPP_HEIDPI_SRVD_H

#include <string>
#include <vector>
#include <list>
#include <tuple>
#include <regex>
#include <stdexcept>
#include <memory>
#include <functional>
#include <iostream>
#include <exception>

#include <nlohmann/json.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "exprtk.hpp"
#include <nlohmann/json-schema.hpp>

#include <chrono>
#include "yaml-cpp/yaml.h"

inline std::unordered_map<std::string, nlohmann::json_schema::json_validator> schema;


// Netzwerk-Puffergrößen (bitte mit config.h synchron halten)
constexpr int NETWORK_BUFFER_MIN_SIZE = 6;       // NETWORK_BUFFER_LENGTH_DIGITS + 1
constexpr int NETWORK_BUFFER_MAX_SIZE = 33792;   // Muss mit config.h übereinstimmen
constexpr int nDPId_PACKETS_PLEN_MAX = 8192;     // Muss mit config.h übereinstimmen

// Ethernet-Typen
constexpr uint16_t PKT_TYPE_ETH_IP4 = 0x0800;
constexpr uint16_t PKT_TYPE_ETH_IP6 = 0x86DD;

class Flow;

class ThreadData {
public:
    uint64_t most_recent_flow_time = 0;
};

class Instance {
public:
    Instance(const std::string& alias, const std::string& source);

    std::string toString() const;

    std::shared_ptr<ThreadData> getThreadData(const std::string& thread_id);
    std::shared_ptr<ThreadData> getThreadDataFromJSON(const nlohmann::json& json_dict);

    uint64_t getMostRecentFlowTime(const std::string& thread_id);
    std::shared_ptr<ThreadData> setMostRecentFlowTime(const std::string& thread_id, uint64_t most_recent_flow_time);

    uint64_t getMostRecentFlowTimeFromJSON(const nlohmann::json& json_dict);
    void setMostRecentFlowTimeFromJSON(const nlohmann::json& json_dict);

    std::unordered_map<int64_t, std::shared_ptr<Flow>> flows;

    std::string alias;
    std::string source;

private:


    std::unordered_map<std::string, std::shared_ptr<ThreadData>> thread_data;
};

class Flow {
public:
    Flow(int64_t flow_id, const std::string& thread_id);

    std::string toString() const;

    int64_t flow_id;
    std::string thread_id;
    int64_t flow_last_seen = -1;
    int64_t flow_idle_time = -1;
    int64_t cleanup_reason = -1;
};

class FlowManager {
public:
    static constexpr int CLEANUP_REASON_INVALID          = 0;
    static constexpr int CLEANUP_REASON_DAEMON_INIT      = 1;
    static constexpr int CLEANUP_REASON_DAEMON_SHUTDOWN  = 2;
    static constexpr int CLEANUP_REASON_FLOW_END         = 3;
    static constexpr int CLEANUP_REASON_FLOW_IDLE        = 4;
    static constexpr int CLEANUP_REASON_FLOW_TIMEOUT     = 5;
    static constexpr int CLEANUP_REASON_APP_SHUTDOWN     = 6;

    FlowManager();

    std::shared_ptr<Instance> getInstance(const nlohmann::json& json_dict);
    static int64_t getLastPacketTime(std::shared_ptr<Instance> instance, int64_t flow_id, const nlohmann::json& json_dict);
    std::shared_ptr<Flow> getFlow(std::shared_ptr<Instance> instance, const nlohmann::json& json_dict);
    std::unordered_map<int64_t, std::shared_ptr<Flow>> getFlowsToCleanup(std::shared_ptr<Instance> instance, const nlohmann::json& json_dict);
    std::unordered_map<int64_t, std::shared_ptr<Flow>> doShutdown();
    std::vector<int64_t> verifyFlows();

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<Instance>>> instances;
};


class nDPIsrvdException : public std::exception {
public:
    enum Type {
        UNSUPPORTED_ADDRESS_TYPE = 1,
        BUFFER_CAPACITY_REACHED  = 2,
        SOCKET_CONNECTION_BROKEN = 3,
        INVALID_LINE_RECEIVED    = 4,
        CALLBACK_RETURNED_FALSE  = 5,
        SOCKET_TIMEOUT           = 6
    };

    explicit nDPIsrvdException(Type type);
    nDPIsrvdException(const std::string& message);

    const char* what() const noexcept override;

    Type etype;

private:
    std::string message;
};

class UnsupportedAddressType : public nDPIsrvdException {
public:
    explicit UnsupportedAddressType(const std::string& addr);

    const char* what() const noexcept override;

private:
    std::string addr;
};

class BufferCapacityReached : public nDPIsrvdException {
public:
    BufferCapacityReached(std::size_t current_length, std::size_t max_length);

    const char* what() const noexcept override;

private:
    std::size_t current_length;
    std::size_t max_length;
    std::string message;
};

class SocketConnectionBroken : public nDPIsrvdException {
public:
    SocketConnectionBroken();

    const char* what() const noexcept override;

private:
    std::string message = "Disconnected.";
};

class InvalidLineReceived : public nDPIsrvdException {
public:
    explicit InvalidLineReceived(const std::string& packet_buffer);

    const char* what() const noexcept override;

private:
    std::string packet_buffer;
    std::string message = "Received JSON line is invalid.";
};

class CallbackReturnedFalse : public nDPIsrvdException {
public:
    CallbackReturnedFalse();

    const char* what() const noexcept override;

private:
    std::string message = "Callback returned False, abort.";
};

class SocketTimeout : public nDPIsrvdException {
public:
    SocketTimeout();

    const char* what() const noexcept override;

private:
    std::string message = "Socket timeout.";
};

class JsonFilter {
public:
    explicit JsonFilter(const std::string& filter_expression);
    bool evaluate(const nlohmann::json& json_dict);

private:
    std::string expression_string;

    double bytes = 0.0;
    double proto_code = 0.0;

    exprtk::symbol_table<double> symbol_table;
    exprtk::expression<double> expression;
    exprtk::parser<double> parser;

    std::unordered_map<std::string, double> proto_map = {
        {"HTTP", 0.0},
        {"HTTPS", 1.0},
        {"FTP", 2.0},
        {"DNS", 3.0}
        // erweitere das hier nach Bedarf
    };
};


class nDPIsrvdSocket {
public:
    nDPIsrvdSocket();

    void addFilter(const std::string& filter_str);
    bool evalFilters(const nlohmann::json& json_dict);

    void connectTo(const std::string& addr);
    void connectTo(const std::string& host, int port);
    void setTimeout(int seconds);
    bool receive();

    bool parse(
        const std::function<bool(const nlohmann::json&, std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_json,
        const std::function<bool(std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_flow_cleanup,
        void* global_user_data
    );

    void loop(
        const std::function<bool(const nlohmann::json&, std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_json,
        const std::function<bool(std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_flow_cleanup,
        void* global_user_data
    );

    std::vector<std::pair<int64_t, std::shared_ptr<Flow>>> shutdown();
    std::vector<int64_t> verify();

private:
    int sock_fd;
    int sock_family;
    std::vector<std::unique_ptr<JsonFilter>> json_filter;
    std::shared_ptr<FlowManager> flow_mgr;
    size_t received_bytes;

    std::vector<uint8_t> buffer;
    size_t msglen;
    size_t digitlen;

    std::list<std::tuple<std::vector<uint8_t>, size_t, size_t>> lines;
    std::list<std::tuple<std::vector<uint8_t>, size_t, size_t>> failed_lines;
    size_t filtered_lines;
};

inline double toSeconds(int64_t usec);

void initSchemaValidator(const std::vector<std::string>& schema_dirs = {});

bool validateAgainstSchema(const nlohmann::json& json_dict);


#endif //HEIDPI_CPP_HEIDPI_SRVD_H