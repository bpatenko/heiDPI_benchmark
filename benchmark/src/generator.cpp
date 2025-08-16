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
#include <random>
#include <cstdlib>

using json = nlohmann::json;

enum class EventType { Flow, Daemon, Error, Packet };

static EventType pickEventType(std::mt19937& rng,
                               const EventProbabilities& probs) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double x = dist(rng);
    double cumulative = probs.flow;
    if (x < cumulative) return EventType::Flow;
    cumulative += probs.daemon;
    if (x < cumulative) return EventType::Daemon;
    cumulative += probs.error;
    if (x < cumulative) return EventType::Error;
    return EventType::Packet;
}

static json buildFlowEvent(uint64_t packetId,
                           uint64_t flowEventId,
                           uint64_t flowId,
                           uint64_t ts_usec) {
    return {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"thread_id", 0},
        {"packet_id", packetId},
        {"flow_event_id", flowEventId},
        {"flow_event_name", "update"},
        {"flow_id", flowId},
        {"flow_state", "info"},
        {"flow_src_packets_processed", 1},
        {"flow_dst_packets_processed", 1},
        {"flow_first_seen", ts_usec},
        {"flow_src_last_pkt_time", ts_usec},
        {"flow_dst_last_pkt_time", ts_usec},
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
        {"thread_ts_usec", ts_usec},
        {"src_ip", "192.168.0.1"},
        {"dst_ip", "192.168.0.2"}
    };
}

static json buildDaemonEvent(uint64_t packetId,
                             uint64_t daemonEventId,
                             uint64_t ts_usec) {
    return {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"thread_id", 0},
        {"packet_id", packetId},
        {"daemon_event_id", daemonEventId},
        {"daemon_event_name", "status"},
        {"packets-captured", 0},
        {"packets-processed", 0},
        {"total-skipped-flows", 0},
        {"total-l4-payload-len", 0},
        {"total-not-detected-flows", 0},
        {"total-guessed-flows", 0},
        {"total-detected-flows", 0},
        {"total-detection-updates", 0},
        {"total-updates", 0},
        {"current-active-flows", 0},
        {"total-active-flows", 0},
        {"total-idle-flows", 0},
        {"total-compressions", 0},
        {"total-compression-diff", 0},
        {"current-compression-diff", 0},
        {"total-events-serialized", 0},
        {"global_ts_usec", ts_usec}
    };
}

static json buildErrorEvent(uint64_t packetId,
                            uint64_t errorEventId,
                            uint64_t ts_usec) {
    return {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"thread_id", 0},
        {"packet_id", packetId},
        {"error_event_id", errorEventId},
        {"error_event_name", "Unknown packet type"},
        {"datalink", 1},
        {"threshold_n", 1},
        {"threshold_n_max", 1},
        {"threshold_time", 1},
        {"threshold_ts_usec", ts_usec},
        {"layer_type", 1},
        {"global_ts_usec", ts_usec}
    };
}

static json buildPacketEvent(uint64_t packetId,
                             uint64_t packetEventId,
                             uint64_t& flowId,
                             uint64_t& flowPacketId,
                             uint64_t ts_usec) {
    json j = {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"packet_id", packetId},
        {"packet_event_id", packetEventId},
        {"pkt_caplen", 64},
        {"pkt_type", 0},
        {"pkt_l3_offset", 14},
        {"pkt_l4_offset", 34},
        {"pkt_len", 64},
        {"pkt_l4_len", 20},
        {"thread_ts_usec", ts_usec}
    };
    if (std::rand() % 2 == 0) {
        j["packet_event_name"] = "packet";
    } else {
        j["packet_event_name"] = "packet-flow";
        j["thread_id"] = 0;
        j["flow_id"] = ++flowId;
        j["flow_packet_id"] = flowPacketId++;
        j["flow_src_last_pkt_time"] = ts_usec;
        j["flow_dst_last_pkt_time"] = ts_usec;
        j["flow_idle_time"] = 10;
    }
    return j;
}

void startGenerator(int client,
                    std::atomic<bool>& running,
                    const EventProbabilities& probs) {
    // allow the logger some time to initialize
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto nextSend = std::chrono::steady_clock::now();
    auto lastPrint = nextSend;
    auto nextPrint = nextSend + std::chrono::milliseconds(500);
    uint64_t lastPacket = 0;
    status::updateRate(0.0);

    uint64_t packetId = 0;
    uint64_t flowEventId = 0;
    uint64_t daemonEventId = 0;
    uint64_t errorEventId = 0;
    uint64_t packetEventId = 0;
    uint64_t flowId = 0;
    uint64_t flowPacketId = 0;

    std::mt19937 rng(std::random_device{}());

    std::string message;
    message.reserve(512);

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
            uint64_t ts_usec =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

            json j;
            EventType type = pickEventType(rng, probs);
            switch (type) {
            case EventType::Flow:
                j = buildFlowEvent(packetId, flowEventId, ++flowId, ts_usec);
                ++flowEventId;
                break;
            case EventType::Daemon:
                j = buildDaemonEvent(packetId, daemonEventId, ts_usec);
                ++daemonEventId;
                break;
            case EventType::Error:
                j = buildErrorEvent(packetId, errorEventId, ts_usec);
                ++errorEventId;
                break;
            case EventType::Packet:
                j = buildPacketEvent(packetId, packetEventId, flowId, flowPacketId,
                                     ts_usec);
                ++packetEventId;
                break;
            }
            ++packetId;

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
            double rate = elapsed > 0 ? (packetId - lastPacket) / elapsed : 0.0;
            status::updateRate(rate);
            status::printStatus();
            lastPrint = now;
            lastPacket = packetId;
            nextPrint += std::chrono::milliseconds(500);
        }
    }
}

