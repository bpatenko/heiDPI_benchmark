#include "generator.h"
#include "scenario.h"
#include "status.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>
#include <sys/socket.h>

using json = nlohmann::json;

// Enumeration of the four possible event types.  We randomly pick
// between them based on the configured probabilities.
enum class EventType { Flow, Daemon, Error, Packet };

// Return the next event type given a random engine and set of
// probabilities.  The cumulative probabilities (flow, daemon,
// error, packet) must add up to 1.0.
static EventType pickEventType(std::mt19937 &rng,
                               const EventProbabilities &probs) {
    std::uniform_real_distribution<> dist(0.0, 1.0);
    double x = dist(rng);
    double cumulative = probs.flow;
    if (x < cumulative)
        return EventType::Flow;
    cumulative += probs.daemon;
    if (x < cumulative)
        return EventType::Daemon;
    cumulative += probs.error;
    if (x < cumulative)
        return EventType::Error;
    return EventType::Packet;
}

// Build a dummy flow event.  Flow events are not the subject of the
// changes requested, so they remain untouched here.
static json buildFlowEvent(uint64_t packetId, uint64_t flowEventId,
                           uint64_t flowId, uint64_t ts_usec) {
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

// Build a daemon event.  Only the configuration fields defined in
// `daemon_event_schema.json` are emitted here.  While some example
// events contained additional metadata (for example `version` and
// `ndpi_version`), the schema sets `additionalProperties` to false,
// meaning any keys not explicitly listed are forbidden.  Therefore
// we deliberately omit such fields.
static json buildDaemonEvent(uint64_t packetId, uint64_t daemonEventId,
                             uint64_t ts_usec) {
    return {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"thread_id", 0},
        {"packet_id", packetId},
        {"daemon_event_id", daemonEventId},
        {"daemon_event_name", "init"},
        // No extra metadata such as version or ndpi_version is emitted
        {"max-flows-per-thread", 2048},
        {"max-idle-flows-per-thread", 64},
        {"reader-thread-count", 10},
        {"flow-scan-interval", 10000000},
        {"generic-max-idle-time", 600000000},
        {"icmp-max-idle-time", 120000000},
        {"udp-max-idle-time", 180000000},
        {"tcp-max-idle-time", 7560000000},
        {"max-packets-per-flow-to-send", 15},
        {"max-packets-per-flow-to-process", 32},
        {"max-packets-per-flow-to-analyse", 32},
        {"global_ts_usec", ts_usec}
    };
}

// Build an error event.  According to `error_event_schema.json` the
// fields `threshold_n`, `threshold_n_max`, `threshold_time` and
// `threshold_ts_usec` are mandatory for every error event.  In a
// previous version of this generator these fields were omitted based
// on a misinterpretation of some example data.  They have been
// restored here to ensure conformity with the schema.  The optional
// `thread_id` field is not included because the "Unknown packet type"
// error does not require it.
static json buildErrorEvent(uint64_t packetId, uint64_t errorEventId,
                            uint64_t ts_usec) {
    return {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"packet_id", packetId},
        {"error_event_id", errorEventId},
        {"error_event_name", "Unknown packet type"},
        // Datalink type (e.g. Ethernet/IP4)
        {"datalink", 1},
        // Required threshold fields
        {"threshold_n", 1},
        {"threshold_n_max", 1},
        {"threshold_time", 1},
        {"threshold_ts_usec", ts_usec},
        {"global_ts_usec", ts_usec}
    };
}

// Build a packet event.  According to `packet_event_schema.json` the
// allowed fields include various packet offsets and lengths along
// with an optional `pkt` payload.  The deprecated `pkt_datalink`
// field from an earlier revision has been removed here.  The
// original generator always emitted a zero `pkt_type`; that remains
// unchanged but callers may adjust the values as needed.
static json buildPacketEvent(uint64_t packetId, uint64_t packetEventId,
                             uint64_t &flowId, uint64_t &flowPacketId,
                             uint64_t ts_usec) {
    return {
        {"alias", "benchmark"},
        {"source", "benchmark"},
        {"thread_id", 0},
        {"packet_id", packetId},
        {"packet_event_id", packetEventId},
        {"packet_event_name", "packet-flow"},
        {"flow_id", ++flowId},
        {"flow_packet_id", flowPacketId++},
        {"flow_src_last_pkt_time", ts_usec},
        {"flow_dst_last_pkt_time", ts_usec},
        {"flow_idle_time", 10},
        // Packet header fields defined in the schema
        {"pkt_caplen", 64},
        {"pkt_type", 0},
        {"pkt_l3_offset", 14},
        {"pkt_l4_offset", 34},
        {"pkt_len", 64},
        {"pkt_l4_len", 20},
        {"thread_ts_usec", ts_usec},
        // Provide an empty payload; real implementations would fill
        // this with base64-encoded packet data
        {"pkt", ""}
    };
}

// Main generator loop.  This function repeatedly picks a random
// event type, builds the corresponding JSON object and sends it
// through the provided socket.  Only the definitions for the
// individual events above have been changed.
void startGenerator(int client, std::atomic<bool> &running,
                    const EventProbabilities &probs) {
    // Give the logger time to initialize
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
                j = buildPacketEvent(packetId, packetEventId, flowId,
                                     flowPacketId, ts_usec);
                ++packetEventId;
                break;
            }
            ++packetId;

            // Serialize the JSON to a string.  We prefix the output with a
            // five‑character decimal length so that the reader knows how
            // many bytes to expect.
            {
                message.clear();
                std::string body = j.dump();
                // Create a five character prefix containing the length of the
                // body.  The length is padded with leading zeros if needed.
                char prefix[6];
                std::snprintf(prefix, sizeof(prefix), "%05zu", body.size());
                message.append(prefix, 5);
                message.append(body);
            }

            // Send the message; if the send fails we log and exit.
            if (::send(client, message.data(), message.size(), 0) < 0) {
                std::cerr << "Generator: send() failed, exiting" << std::endl;
                break;
            }

            // Determine how long to wait until the next event based on
            // the current scenario.  status::updateRate is called to
            // update the reported rate of events per second.
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
                std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                                      lastPrint)
                    .count() /
                1e6;
            double rate =
                elapsed > 0 ? (packetId - lastPacket) / elapsed : 0.0;
            status::updateRate(rate);
            status::printStatus();
            lastPrint = now;
            lastPacket = packetId;
            nextPrint += std::chrono::milliseconds(500);
        }
    }
}