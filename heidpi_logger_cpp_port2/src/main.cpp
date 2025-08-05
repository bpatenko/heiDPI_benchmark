#include "Config.hpp"
#include "Logger.hpp"
#include "NDPIClient.hpp"
#include "EventProcessor.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

struct CLIOptions {
    std::string host{"127.0.0.1"};
    std::string unix_path{};
    int port{7000};
    std::string write_path{"/var/log"};
    std::string config_path{"config.yml"};
    std::string filter{};
    bool show_daemon{false};
    bool show_packet{false};
    bool show_error{false};
    bool show_flow{false};
};

static std::string envOrDefault(const char *env, const std::string &def) {
    const char *v = std::getenv(env);
    return v ? std::string(v) : def;
}

CLIOptions parse(int argc, char **argv) {
    CLIOptions o;
    o.host = envOrDefault("HOST", o.host);
    o.unix_path = envOrDefault("UNIX", o.unix_path);
    o.port = std::stoi(envOrDefault("PORT", std::to_string(o.port)));
    o.write_path = envOrDefault("WRITE", o.write_path);
    o.config_path = envOrDefault("CONFIG", o.config_path);
    o.filter = envOrDefault("FILTER", o.filter);
    o.show_daemon = envOrDefault("SHOW_DAEMON_EVENTS", "0") == "1";
    o.show_packet = envOrDefault("SHOW_PACKET_EVENTS", "0") == "1";
    o.show_error = envOrDefault("SHOW_ERROR_EVENTS", "0") == "1";
    o.show_flow = envOrDefault("SHOW_FLOW_EVENTS", "0") == "1";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](int &i){ return std::string(argv[++i]); };
        if (a == "--host" && i+1 < argc) o.host = next(i);
        else if (a == "--unix" && i+1 < argc) o.unix_path = next(i);
        else if (a == "--port" && i+1 < argc) o.port = std::stoi(next(i));
        else if (a == "--write" && i+1 < argc) o.write_path = next(i);
        else if (a == "--config" && i+1 < argc) o.config_path = next(i);
        else if (a == "--filter" && i+1 < argc) o.filter = next(i);
        else if (a == "--show-daemon-events") o.show_daemon = !o.show_daemon;
        else if (a == "--show-packet-events") o.show_packet = !o.show_packet;
        else if (a == "--show-error-events") o.show_error = !o.show_error;
        else if (a == "--show-flow-events") o.show_flow = !o.show_flow;
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --host <host>            Set host\n"
                      << "  --unix <path>            Set unix socket path\n"
                      << "  --port <port>            Set port\n"
                      << "  --write <path>           Set write path\n"
                      << "  --config <path>          Set config path\n"
                      << "  --filter <expr>          Filter expression\n"
                      << "  --show-daemon-events     Toggle daemon events\n"
                      << "  --show-packet-events     Toggle packet events\n"
                      << "  --show-error-events      Toggle error events\n"
                      << "  --show-flow-events       Toggle flow events\n"
                      << "  -h, --help               Show this help message\n";
            std::exit(0);
        }
    }
    return o;
}

static void usage(const char *prog) {
    std::string name = std::filesystem::path(prog).filename();
    std::cout << "usage: " << name
              << " [-h] [--host HOST | --unix UNIX] [--port PORT] [--write WRITE]\n"
                 "            [--config CONFIG] [--filter FILTER]\n"
                 "            [--show-daemon-events SHOW_DAEMON_EVENTS]\n"
                 "            [--show-packet-events SHOW_PACKET_EVENTS]\n"
                 "            [--show-error-events SHOW_ERROR_EVENTS]\n"
                 "            [--show-flow-events SHOW_FLOW_EVENTS]\n\n"
                 "heiDPI Python Interface\n\n"
                 "options:\n"
                 "  -h, --help            show this help message and exit\n"
                 "  --host HOST           nDPIsrvd host IP\n"
                 "  --unix UNIX           nDPIsrvd unix socket path\n"
                 "  --port PORT           nDPIsrvd TCP port (default: 7000)\n"
                 "  --write WRITE         heiDPI write path for logs (default: /var/log)\n"
                 "  --config CONFIG       heiDPI write path for logs (default: config.yml)\n"
                 "  --filter FILTER       nDPId filter string, e.g. --filter 'ndpi' in json_dict and 'proto' in json_dict['ndpi'] (default: )\n"
                 "  --show-daemon-events SHOW_DAEMON_EVENTS\n"
                 "                        heiDPI shows daemon events (default: 0)\n"
                 "  --show-packet-events SHOW_PACKET_EVENTS\n"
                 "                        heiDPI shows packet events (default: 0)\n"
                 "  --show-error-events SHOW_ERROR_EVENTS\n"
                 "                        heiDPI shows error events (default: 0)\n"
                 "  --show-flow-events SHOW_FLOW_EVENTS\n"
                 "                        heiDPI shows flow events (default: 0)\n";
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        }
    }
    CLIOptions opts = parse(argc, argv);
    Config cfg(opts.config_path);
    Logger::init(cfg.logging());

    std::vector<std::thread> threads;
    auto start = [&](bool enable, const EventConfig &ec) {
        if (!enable) return;
        threads.emplace_back([&, ec] {
            NDPIClient client;
            if (!opts.unix_path.empty())
                client.connectUnix(opts.unix_path);
            else
                client.connectTcp(opts.host, static_cast<unsigned short>(opts.port));
            EventProcessor proc(ec, opts.write_path);
            client.loop([&](const nlohmann::json &j) {
                // check allowed event names
                std::string name;
                if (j.contains("flow_event_name")) name = j["flow_event_name"].get<std::string>();
                if (j.contains("packet_event_name")) name = j["packet_event_name"].get<std::string>();
                if (j.contains("daemon_event_name")) name = j["daemon_event_name"].get<std::string>();
                if (j.contains("error_event_name")) name = j["error_event_name"].get<std::string>();
                if (ec.event_names.empty() ||
                    std::find(ec.event_names.begin(), ec.event_names.end(), name) != ec.event_names.end()) {
                    proc.process(j);
                } else {
                    std::ostringstream ss;
                    ss << "[";
                    for (size_t i = 0; i < ec.event_names.size(); ++i) {
                        ss << ec.event_names[i];
                        if (i + 1 < ec.event_names.size()) ss << ", ";
                    }
                    ss << "]";
                    Logger::info("Ignoring event '" + name + "' not in allowed list " + ss.str());
                }
                return; }, opts.filter);
        });
    };

    start(opts.show_flow, cfg.flowEvent());
    start(opts.show_packet, cfg.packetEvent());
    start(opts.show_daemon, cfg.daemonEvent());
    start(opts.show_error, cfg.errorEvent());

    for (auto &t : threads) t.join();
    return 0;
}

