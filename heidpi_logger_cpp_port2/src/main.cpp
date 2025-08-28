#include "Config.hpp"
#include "Logger.hpp"
#include "NDPIClient.hpp"
#include "EventProcessor.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <iostream>
#include <chrono>
#include <iomanip>

// additional headers for queue buffering
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// Same CLI options structure as original
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

// Helper to read environment variables or fall back to a default
static std::string envOrDefault(const char *env, const std::string &def) {
    const char *v = std::getenv(env);
    return v ? std::string(v) : def;
}

// Parse command line and environment into CLIOptions
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

// Struct to bundle an event handler with its configuration
struct Worker {
    std::string eventKey;      // key in JSON indicating this event type
    EventConfig config;        // copy of configuration for this type
    EventProcessor processor;  // processor instance
    Worker(const std::string &k, const EventConfig &c, const std::string &dir)
        : eventKey(k), config(c), processor(c, dir) {}
};

int main(int argc, char **argv) {
    // quick help check
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::string name = std::filesystem::path(argv[0]).filename();
            std::cout << "usage: " << name
                      << " [-h] [--host HOST | --unix UNIX] [--port PORT] [--write WRITE]\n"
                         "            [--config CONFIG] [--filter FILTER]\n"
                         "            [--show-daemon-events]\n"
                         "            [--show-packet-events]\n"
                         "            [--show-error-events]\n"
                         "            [--show-flow-events]\n";
            return 0;
        }
    }
    // parse options
    CLIOptions opts = parse(argc, argv);
    // load configuration
    Config cfg(opts.config_path);
    // initialize logger
    Logger::init(cfg.logging());

    // Build a list of workers (event type + processor) according to CLI flags
    std::vector<Worker> workers;
    workers.reserve(4);
    if (opts.show_flow) {
        workers.emplace_back("flow_event_name", cfg.flowEvent(), opts.write_path);
    }
    if (opts.show_packet) {
        workers.emplace_back("packet_event_name", cfg.packetEvent(), opts.write_path);
    }
    if (opts.show_daemon) {
        workers.emplace_back("daemon_event_name", cfg.daemonEvent(), opts.write_path);
    }
    if (opts.show_error) {
        workers.emplace_back("error_event_name", cfg.errorEvent(), opts.write_path);
    }
    if (workers.empty()) {
        Logger::error("No event types enabled. Use --show-*_events flags to enable processing.");
        return 1;
    }

    // Start single client connection
    NDPIClient client;
    try {
        if (!opts.unix_path.empty())
            client.connectUnix(opts.unix_path);
        else
            client.connectTcp(opts.host, static_cast (opts.port));
    } catch (const std::exception &ex) {
        Logger::error(std::string("Failed to connect: ") + ex.what());
        return 1;
    }

    // queue and synchronization primitives
    std::queue  eventQueue;
    std::mutex qMutex;
    std::condition_variable qCV;
    std::atomic  done(false);

    // dispatcher thread that consumes events from the queue and processes them
    std::thread dispatcher([&]() {
        while (!done.load() || !eventQueue.empty()) {
            std::unique_lock lock(qMutex);
            qCV.wait(lock, [&]() {
                return done.load() || !eventQueue.empty();
            });
            if (eventQueue.empty()) {
                continue;
            }
            // pop next event
            nlohmann::json event = std::move(eventQueue.front());
            eventQueue.pop();
            lock.unlock();

            // Determine the event key and name; default to empty
            std::string key;
            std::string name;
            if (event.contains("flow_event_name")) {
                key = "flow_event_name";
                name = event["flow_event_name"].get ();
            } else if (event.contains("packet_event_name")) {
                key = "packet_event_name";
                name = event["packet_event_name"].get ();
            } else if (event.contains("daemon_event_name")) {
                key = "daemon_event_name";
                name = event["daemon_event_name"].get ();
            } else if (event.contains("error_event_name")) {
                key = "error_event_name";
                name = event["error_event_name"].get ();
            } else {
                // unknown event type
                Logger::info("Received unknown event: missing event name");
                continue;
            }
            // Dispatch event to matching worker(s)
            bool handled = false;
            for (auto &w : workers) {
                if (w.eventKey != key) continue;
                w.processor.process(event);
                handled = true;
            }
            if (!handled) {
                // No worker configured for this event type
                Logger::info("No handler enabled for event '" + name + "' of type " + key);
            }
        }
    });

    // Process events: read them and push to the queue. We do not process them
    // here to avoid blocking the socket reader. The callback simply enqueues
    // each JSON payload and notifies the dispatcher.
    client.loop([&](const nlohmann::json &j) {
        {
            std::lock_guard lock(qMutex);
            eventQueue.push(j);
        }
        qCV.notify_one();
    }, opts.filter);

    // signal that no more events will arrive and wait for the dispatcher to
    // finish processing the remaining buffered events
    done = true;
    qCV.notify_all();
    dispatcher.join();

    return 0;
}