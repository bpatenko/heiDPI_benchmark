//
// Created by Bogdan Patenko on 29.07.25.
//

#include "heidpi_srvd.h"
#include <sstream>
#include <algorithm>


Instance::Instance(const std::string& alias, const std::string& source)
    : alias(alias), source(source) {}

std::string Instance::toString() const {
    std::ostringstream oss;
    oss << "<Instance object at " << this
        << " with alias " << alias
        << ", source " << source << ">";
    return oss.str();
}

std::shared_ptr<ThreadData> Instance::getThreadData(const std::string& thread_id) {
    auto it = thread_data.find(thread_id);
    if (it == thread_data.end()) return nullptr;
    return it->second;
}

std::shared_ptr<ThreadData> Instance::getThreadDataFromJSON(const nlohmann::json& json_dict) {
    if (!json_dict.contains("thread_id")) return nullptr;
    return getThreadData(json_dict["thread_id"].get<std::string>());
}

uint64_t Instance::getMostRecentFlowTime(const std::string& thread_id) {
    return thread_data[thread_id]->most_recent_flow_time;
}

std::shared_ptr<ThreadData> Instance::setMostRecentFlowTime(const std::string& thread_id, uint64_t most_recent_flow_time) {
    auto it = thread_data.find(thread_id);
    if (it != thread_data.end()) return it->second;

    auto td = std::make_shared<ThreadData>();
    td->most_recent_flow_time = most_recent_flow_time;
    thread_data[thread_id] = td;
    return td;
}

uint64_t Instance::getMostRecentFlowTimeFromJSON(const nlohmann::json& json_dict) {
    if (!json_dict.contains("thread_id")) return 0;
    auto td = getThreadData(json_dict["thread_id"].get<std::string>());
    return td ? td->most_recent_flow_time : 0;
}

void Instance::setMostRecentFlowTimeFromJSON(const nlohmann::json& json_dict) {
    if (!json_dict.contains("thread_id")) return;

    std::string thread_id = json_dict["thread_id"].get<std::string>();
    uint64_t current_mrtf = thread_data.contains(thread_id)
        ? thread_data[thread_id]->most_recent_flow_time
        : 0;

    if (json_dict.contains("thread_ts_usec")) {
        uint64_t ts = json_dict["thread_ts_usec"].get<uint64_t>();
        setMostRecentFlowTime(thread_id, std::max(ts, current_mrtf));
    }
}

Flow::Flow(int64_t flow_id, const std::string& thread_id)
    : flow_id(flow_id), thread_id(thread_id) {}

std::string Flow::toString() const {
    std::ostringstream oss;
    oss << "<Flow object at " << this
        << " with flow id " << flow_id << ">";
    return oss.str();
}


FlowManager::FlowManager() = default;

std::shared_ptr<Instance> FlowManager::getInstance(const nlohmann::json& json_dict) {
    if (!json_dict.contains("alias") || !json_dict.contains("source"))
        return nullptr;

    std::string alias = json_dict["alias"].get<std::string>();
    std::string source = json_dict["source"].get<std::string>();

    if (!instances.contains(alias))
        instances[alias] = {};

    if (!instances[alias].contains(source)) {
        instances[alias][source] = std::make_shared<Instance>(alias, source);
    }

    instances[alias][source]->setMostRecentFlowTimeFromJSON(json_dict);
    return instances[alias][source];
}

int64_t FlowManager::getLastPacketTime(std::shared_ptr<Instance> instance, int64_t flow_id, const nlohmann::json& json_dict) {
    return std::max({
        json_dict["flow_src_last_pkt_time"].get<int64_t>(),
        json_dict["flow_dst_last_pkt_time"].get<int64_t>(),
        instance->flows[flow_id]->flow_last_seen
    });
}

std::shared_ptr<Flow> FlowManager::getFlow(std::shared_ptr<Instance> instance, const nlohmann::json& json_dict) {
    if (!json_dict.contains("flow_id"))
        return nullptr;

    int64_t flow_id = json_dict["flow_id"].get<int64_t>();

    if (instance->flows.contains(flow_id)) {
        auto& flow = instance->flows[flow_id];
        flow->flow_last_seen = getLastPacketTime(instance, flow_id, json_dict);
        flow->flow_idle_time = json_dict["flow_idle_time"].get<int64_t>();
        return flow;
    }

    int64_t thread_id = json_dict["thread_id"].get<int64_t>();
    auto new_flow = std::make_shared<Flow>(flow_id, std::to_string(thread_id));
    new_flow->flow_last_seen = getLastPacketTime(instance, flow_id, json_dict);
    new_flow->flow_idle_time = json_dict["flow_idle_time"].get<int64_t>();
    new_flow->cleanup_reason = CLEANUP_REASON_INVALID;

    instance->flows[flow_id] = new_flow;
    return new_flow;
}

std::unordered_map<int64_t, std::shared_ptr<Flow>> FlowManager::getFlowsToCleanup(std::shared_ptr<Instance> instance, const nlohmann::json& json_dict) {
    std::unordered_map<int64_t, std::shared_ptr<Flow>> flows;

    if (json_dict.contains("daemon_event_name")) {
        std::string evt = json_dict["daemon_event_name"].get<std::string>();
        std::transform(evt.begin(), evt.end(), evt.begin(), ::tolower);
        if (evt == "init" || evt == "shutdown") {
            int64_t thread_id = json_dict["thread_id"].get<int64_t>();

            for (auto it = instance->flows.begin(); it != instance->flows.end();) {
                auto& flow = it->second;
                if (std::stoi(flow->thread_id) != thread_id) {
                    ++it;
                    continue;
                }

                flow->cleanup_reason = (evt == "init")
                    ? CLEANUP_REASON_DAEMON_INIT
                    : CLEANUP_REASON_DAEMON_SHUTDOWN;

                flows[it->first] = flow;
                it = instance->flows.erase(it);
            }

            if (instance->flows.empty()) {
                instances[instance->alias].erase(instance->source);
            }
        }
    }
    else if (json_dict.contains("flow_event_name")) {
        std::string evt = json_dict["flow_event_name"].get<std::string>();
        std::transform(evt.begin(), evt.end(), evt.begin(), ::tolower);

        int64_t flow_id = json_dict["flow_id"].get<int64_t>();

        if (evt == "end") {
            instance->flows[flow_id]->cleanup_reason = CLEANUP_REASON_FLOW_END;
        } else if (evt == "idle") {
            instance->flows[flow_id]->cleanup_reason = CLEANUP_REASON_FLOW_IDLE;
        }

        if (evt != "guessed" && evt != "not-detected" && evt != "detected") {
            flows[flow_id] = instance->flows[flow_id];
            instance->flows.erase(flow_id);
        }
    }
    else if (json_dict.contains("flow_last_seen")) {
        int64_t last_seen = json_dict["flow_last_seen"].get<int64_t>();
        int64_t idle_time = json_dict["flow_idle_time"].get<int64_t>();
        if (last_seen + idle_time < instance->getMostRecentFlowTimeFromJSON(json_dict)) {
            int64_t flow_id = json_dict["flow_id"].get<int64_t>();
            instance->flows[flow_id]->cleanup_reason = CLEANUP_REASON_FLOW_TIMEOUT;
            flows[flow_id] = instance->flows[flow_id];
            instance->flows.erase(flow_id);
        }
    }

    return flows;
}

std::unordered_map<int64_t, std::shared_ptr<Flow>> FlowManager::doShutdown() {
    std::unordered_map<int64_t, std::shared_ptr<Flow>> flows;

    for (auto& [alias, sources] : instances) {
        for (auto& [source, instance] : sources) {
            for (auto& [flow_id, flow] : instance->flows) {
                flow->cleanup_reason = CLEANUP_REASON_APP_SHUTDOWN;
                flows[flow_id] = flow;
            }
        }
    }

    instances.clear();
    return flows;
}

std::vector<int64_t> FlowManager::verifyFlows() {
    std::vector<int64_t> invalid_flows;

    for (auto& [alias, sources] : instances) {
        for (auto& [source, instance] : sources) {
            for (auto& [flow_id, flow] : instance->flows) {
                int64_t thread_id = std::stoll(flow->thread_id);
                if (flow->flow_last_seen + flow->flow_idle_time <
                    instance->getMostRecentFlowTime(std::to_string(thread_id))) {
                    invalid_flows.push_back(flow_id);
                }
            }
        }
    }

    return invalid_flows;
}


nDPIsrvdException::nDPIsrvdException(Type type)
    : etype(type), message("nDPIsrvdException type " + std::to_string(static_cast<int>(type))) {}

nDPIsrvdException::nDPIsrvdException(const std::string& message)
    : etype(Type::INVALID_LINE_RECEIVED), message(message) {}


const char* nDPIsrvdException::what() const noexcept {
    return message.c_str();
}

UnsupportedAddressType::UnsupportedAddressType(const std::string& addr)
    : nDPIsrvdException(Type::UNSUPPORTED_ADDRESS_TYPE), addr(addr) {}

const char* UnsupportedAddressType::what() const noexcept {
    return addr.c_str();
}

BufferCapacityReached::BufferCapacityReached(std::size_t current_length, std::size_t max_length)
    : nDPIsrvdException(Type::BUFFER_CAPACITY_REACHED),
      current_length(current_length),
      max_length(max_length)
{
    message = std::to_string(current_length) + " of " + std::to_string(max_length) + " bytes";
}

const char* BufferCapacityReached::what() const noexcept {
    return message.c_str();
}

SocketConnectionBroken::SocketConnectionBroken()
    : nDPIsrvdException(Type::SOCKET_CONNECTION_BROKEN) {}

const char* SocketConnectionBroken::what() const noexcept {
    return message.c_str();
}

InvalidLineReceived::InvalidLineReceived(const std::string& packet_buffer)
    : nDPIsrvdException(Type::INVALID_LINE_RECEIVED),
      packet_buffer(packet_buffer) {}

const char* InvalidLineReceived::what() const noexcept {
    return message.c_str();
}

CallbackReturnedFalse::CallbackReturnedFalse()
    : nDPIsrvdException(Type::CALLBACK_RETURNED_FALSE) {}

const char* CallbackReturnedFalse::what() const noexcept {
    return message.c_str();
}

SocketTimeout::SocketTimeout()
    : nDPIsrvdException(Type::SOCKET_TIMEOUT) {}

const char* SocketTimeout::what() const noexcept {
    return message.c_str();
}

JsonFilter::JsonFilter(const std::string& filter_expression)
    : expression_string(filter_expression)
{
    symbol_table.add_variable("bytes", bytes);
    symbol_table.add_variable("proto_code", proto_code);

    expression.register_symbol_table(symbol_table);

    if (!parser.compile(expression_string, expression)) {
        throw std::runtime_error("Failed to compile filter expression: " + expression_string);
    }
}

bool JsonFilter::evaluate(const nlohmann::json& json_dict) {
    if (!json_dict.is_object()) {
        throw nDPIsrvdException(nDPIsrvdException::INVALID_LINE_RECEIVED);
    }

    bytes = json_dict.value("bytes", 0.0);

    std::string proto = json_dict.value("proto_name", "");
    if (proto_map.contains(proto)) {
        proto_code = proto_map[proto];
    } else {
        proto_code = -1.0; // unbekannter proto_name
    }

    return expression.value();
}


nDPIsrvdSocket::nDPIsrvdSocket()
    : sock_fd(-1), sock_family(AF_UNSPEC), flow_mgr(std::make_shared<FlowManager>()),
      received_bytes(0), msglen(0), digitlen(0), filtered_lines(0) {}

void nDPIsrvdSocket::addFilter(const std::string& filter_str) {
    json_filter.emplace_back(std::make_unique<JsonFilter>(filter_str));
}

bool nDPIsrvdSocket::evalFilters(const nlohmann::json& json_dict) {
    for (const auto& jf : json_filter) {
        bool result = false;
        try {
            result = jf->evaluate(json_dict);
        } catch (const std::exception& e) {
            std::cerr << "Error while evaluating expression: " << e.what() << std::endl;
            throw;
        }

        if (!result) return false;
    }
    return true;
}

void nDPIsrvdSocket::connectTo(const std::string& addr) {
    sock_family = AF_UNIX;
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) throw std::runtime_error("Failed to create socket");

    sockaddr_un serv_addr{};
    serv_addr.sun_family = AF_UNIX;
    std::strncpy(serv_addr.sun_path, addr.c_str(), sizeof(serv_addr.sun_path) - 1);

    if (connect(sock_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        throw std::runtime_error("Failed to connect to UNIX socket");
    }
}

void nDPIsrvdSocket::connectTo(const std::string& host, int port) {
    sock_family = AF_INET;
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) throw std::runtime_error("Failed to create socket");

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        throw std::runtime_error("Invalid address");
    }

    if (connect(sock_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        throw std::runtime_error("Failed to connect to INET socket");
    }
}

void nDPIsrvdSocket::setTimeout(int seconds) {
    timeval timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

bool nDPIsrvdSocket::receive() {
    if (buffer.size() == NETWORK_BUFFER_MAX_SIZE) {
        throw BufferCapacityReached(buffer.size(), NETWORK_BUFFER_MAX_SIZE);
    }

    std::vector<uint8_t> temp(NETWORK_BUFFER_MAX_SIZE - buffer.size());
    ssize_t recvd = recv(sock_fd, temp.data(), temp.size(), 0);

    if (recvd == 0) {
        throw SocketConnectionBroken();
    } else if (recvd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
            throw SocketTimeout();
        }
        throw std::runtime_error("Socket receive failed");
    }

    buffer.insert(buffer.end(), temp.begin(), temp.begin() + recvd);

    bool new_data_avail = false;
    while (msglen + digitlen <= buffer.size()) {
        if (msglen == 0) {
            std::string header(buffer.begin(), buffer.begin() + std::min(buffer.size(), (size_t)NETWORK_BUFFER_MIN_SIZE));
            std::smatch match;
            std::regex regex(R"((\d+)\{)");

            if (!std::regex_search(header, match, regex)) {
                if (buffer.size() < NETWORK_BUFFER_MIN_SIZE) break;
                throw InvalidLineReceived(std::string(buffer.begin(), buffer.end()));
            }

            msglen = std::stoi(match[1]);
            digitlen = match[1].length();
        }

        if (buffer.size() >= msglen + digitlen) {
            std::vector<uint8_t> recvd(buffer.begin() + digitlen, buffer.begin() + digitlen + msglen);
            lines.emplace_back(recvd, msglen, digitlen);
            buffer.erase(buffer.begin(), buffer.begin() + digitlen + msglen);

            received_bytes += msglen + digitlen;
            msglen = 0;
            digitlen = 0;
            new_data_avail = true;
        } else {
            break;
        }
    }

    return new_data_avail;
}

bool nDPIsrvdSocket::parse(
    const std::function<bool(const nlohmann::json&, std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_json,
    const std::function<bool(std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_flow_cleanup,
    void* global_user_data
) {
    bool retval = true;

    while (!lines.empty()) {
        auto received_line = lines.front();
        lines.pop_front();

        const auto& raw = std::get<0>(received_line);
        nlohmann::json json_dict;
        try {
            json_dict = nlohmann::json::parse(raw.begin(), raw.end());
        } catch (const nlohmann::json::parse_error& e) {
            failed_lines.push_back(received_line);
            throw;
        }

        auto instance = flow_mgr->getInstance(json_dict);
        if (!instance) {
            failed_lines.push_back(received_line);
            retval = false;
            continue;
        }

        auto current_flow = flow_mgr->getFlow(instance, json_dict);
        bool filter_eval = false;
        try {
            filter_eval = evalFilters(json_dict);
        } catch (...) {
            failed_lines.push_back(received_line);
            retval = false;
            continue;
        }

        if (filter_eval) {
            try {
                if (!callback_json(json_dict, instance, current_flow, global_user_data)) {
                    failed_lines.push_back(received_line);
                    retval = false;
                }
            } catch (...) {
                failed_lines.push_back(received_line);
                throw;
            }
        } else {
            filtered_lines++;
        }

        auto cleanup_flows = flow_mgr->getFlowsToCleanup(instance, json_dict);
        for (const auto& [flow_id, flow] : cleanup_flows) {
            if (callback_flow_cleanup && filter_eval) {
                if (!callback_flow_cleanup(instance, flow, global_user_data)) {
                    failed_lines.push_back(received_line);
                    retval = false;
                }
            }
        }
    }

    return retval;
}

void nDPIsrvdSocket::loop(
    const std::function<bool(const nlohmann::json&, std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_json,
    const std::function<bool(std::shared_ptr<Instance>, std::shared_ptr<Flow>, void*)>& callback_flow_cleanup,
    void* global_user_data
) {
    std::exception_ptr throw_ex = nullptr;

    while (true) {
        try {
            receive();
        } catch (...) {
            throw_ex = std::current_exception();
        }

        if (!parse(callback_json, callback_flow_cleanup, global_user_data)) {
            throw CallbackReturnedFalse();
        }

        if (throw_ex) {
            std::rethrow_exception(throw_ex);
        }
    }
}

std::vector<std::pair<int64_t, std::shared_ptr<Flow>>> nDPIsrvdSocket::shutdown() {
    auto cleanup_map = flow_mgr->doShutdown();
    std::vector<std::pair<int64_t, std::shared_ptr<Flow>>> result(cleanup_map.begin(), cleanup_map.end());
    return result;
}

std::vector<int64_t> nDPIsrvdSocket::verify() {
    if (!failed_lines.empty()) {
        throw nDPIsrvdException("Failed lines > 0: " + std::to_string(failed_lines.size()));
    }
    return flow_mgr->verifyFlows();
}

inline double toSeconds(int64_t usec) {
    return static_cast<double>(usec) / (1000.0 * 1000.0);
}

void initSchemaValidator(const std::vector<std::string>& schema_dirs) {
    std::vector<std::string> dirs = schema_dirs;
    if (dirs.empty()) {
        dirs.push_back("./schema");  // fallback
    }

    std::vector<std::string> keys = {
        "packet_event_schema", "error_event_schema",
        "daemon_event_schema", "flow_event_schema"
    };

    for (const auto& key : keys) {
        for (const auto& dir : dirs) {
            std::ifstream file(dir + "/" + key + ".json");
            if (!file) {
                std::cerr << "No schema in " << dir << "\n";
                continue;
            }

            try {
                nlohmann::json schema_json = nlohmann::json::parse(file);
                schema[key].set_root_schema(schema_json);
                break;
            } catch (const std::exception& e) {
                std::cerr << "Schema parse error: " << e.what() << "\n";
            }
        }
    }
}


using nlohmann::json;
using Validator = nlohmann::json_schema::json_validator;

bool validateAgainstSchema(const nlohmann::json& json_dict) {
    try {
        if (json_dict.contains("packet_event_id")) {
            schema["packet_event_schema"].validate(json_dict);
            return true;
        }
        if (json_dict.contains("error_event_id")) {
            schema["error_event_schema"].validate(json_dict);
            return true;
        }
        if (json_dict.contains("daemon_event_id")) {
            schema["daemon_event_schema"].validate(json_dict);
            return true;
        }
        if (json_dict.contains("flow_event_id")) {
            schema["flow_event_schema"].validate(json_dict);
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Schema validation failed: " << e.what() << "\n";
    }

    return false;
}
