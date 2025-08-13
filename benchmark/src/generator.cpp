#include "generator.h"
#include "scenario.h"
#include "status.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <nlohmann/json.hpp>
#include <nlohmann/detail/output/output_adapters.hpp>
#include <nlohmann/detail/output/serializer.hpp>
#include <atomic>
#include <string>
#include <cstdio>

using json = nlohmann::json;

void startGenerator(int client, std::atomic<bool>& running) {
    // allow the logger some time to initialize
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto nextSend = std::chrono::steady_clock::now();
    auto lastPrint = nextSend;
    auto nextPrint = nextSend + std::chrono::milliseconds(500);
    uint64_t lastPacket = 0;
    status::updateRate(0.0);
    uint64_t packet_id = 0;
    uint64_t flow_id = 1;

    // Pre-build JSON message template
    json j = {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"thread_id", 0},
        {"packet_id", packet_id},
        {"flow_event_id", 1},
        {"flow_event_name", "update"},
        {"flow_id", flow_id},
        {"flow_state", "info"},
        {"flow_src_packets_processed", 1},
        {"flow_dst_packets_processed", 1},
        {"flow_first_seen", 0},
        {"flow_src_last_pkt_time", 0},
        {"flow_dst_last_pkt_time", 0},
        {"flow_idle_time", 10},
        {"flow_src_min_l4_payload_len", 0},
        {"flow_dst_min_l4_payload_len", 0},
        {"flow_src_max_l4_payload_len", 0},
        {"flow_dst_max_l4_payload_len", 0},
        {"flow_src_tot_l4_payload_len", 0},
        {"flow_dst_tot_l4_payload_len", 0},
        {"flow_datalink", 1},
        {"flow_max_packets", 10},
        {"l3_proto", "ip4"},
        {"l4_proto", "tcp"},
        {"midstream", 0},
        {"thread_ts_usec", 0},
        {"src_ip", "192.168.0.1"},
        {"dst_ip", "192.168.0.2"}
    };

    // Reusable buffer for serialized messages
    std::string message;
    message.reserve(512);

    // Track current scenario to print a message whenever it changes
    ScenarioPtr lastScenario =
        std::atomic_load_explicit(&gScenario, std::memory_order_acquire);
    std::cout << "[Generator] Scenario " << modeToString(lastScenario->mode)
              << " active" << std::endl;

    status::printStatus();
    lastPrint = std::chrono::steady_clock::now();

    while (running.load()) {
        auto now = std::chrono::steady_clock::now();
        ScenarioPtr currentScenario =
            std::atomic_load_explicit(&gScenario, std::memory_order_acquire);
        if (currentScenario != lastScenario) {
            std::cout << "[Generator] Scenario changed to "
                      << modeToString(currentScenario->mode) << std::endl;
            lastScenario = currentScenario;
        }

        if (now >= nextSend) {
            uint64_t ts_usec = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // update dynamic fields
            j["packet_id"] = packet_id++;
            j["flow_id"] = flow_id;
            j["flow_first_seen"] = ts_usec;
            j["flow_src_last_pkt_time"] = ts_usec;
            j["flow_dst_last_pkt_time"] = ts_usec;
            j["thread_ts_usec"] = ts_usec;

            message.clear();
            message.resize(5); // placeholder for length prefix
            {
                nlohmann::detail::output_adapter<char> oa(message);
                nlohmann::detail::serializer<json> writer(
                    oa, ' ', nlohmann::detail::error_handler_t::strict);
                writer.dump(j, false, false, 0);
            }
            char prefix[6];
            std::snprintf(prefix, sizeof(prefix), "%05zu", message.size() - 5);
            std::memcpy(message.data(), prefix, 5);

            if (send(client, message.data(), message.size(), 0) < 0) {
                std::cerr << "Generator: send() failed, exiting" << std::endl;
                break;
            }

            auto interval = nextInterval(*currentScenario);
            status::updateRate(1'000'000.0 / interval.count());
            nextSend += interval;
        } else {
            auto us = nextInterval(*currentScenario);
            status::updateRate(1'000'000.0 / us.count());
            std::this_thread::sleep_for(us);
        }

        now = std::chrono::steady_clock::now();
        if (now >= nextPrint) {
            double elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(now - lastPrint)
                    .count() /
                1e6;
            double rate = elapsed > 0 ? (packet_id - lastPacket) / elapsed : 0.0;
            status::updateRate(rate);
            status::printStatus();
            lastPrint = now;
            lastPacket = packet_id;
            nextPrint += std::chrono::milliseconds(500);
        }
    }
}
