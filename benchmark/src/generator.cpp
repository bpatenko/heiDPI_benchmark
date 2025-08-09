#include "generator.h"
#include "scenario.h"
#include "status.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <nlohmann/json.hpp>
#include <atomic>

using json = nlohmann::json;

void startGenerator(const GeneratorParams& params,
                    std::atomic<bool>& running,
                    std::atomic<bool>& ready,
                    std::atomic<bool>& startSending) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Generator: socket() failed" << std::endl;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(params.port);
    inet_pton(AF_INET, params.host.c_str(), &serverAddr.sin_addr);

    if (bind(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        perror("Generator: bind failed");
        close(sock);
        return;
    }
    if (listen(sock, 1) < 0) {
        perror("Generator: listen failed");
        close(sock);
        return;
    }

    // Notify main that socket is ready
    ready = true;

    int client = -1;
    while (running.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        struct timeval tv {1, 0};
        int ret = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            perror("Generator: select failed");
            close(sock);
            return;
        } else if (ret == 0) {
            continue;
        }

        if (FD_ISSET(sock, &rfds)) {
            client = accept(sock, nullptr, nullptr);
            if (client < 0) {
                if (running.load()) {
                    perror("Generator: accept failed");
                }
                close(sock);
                return;
            }
            break;
        }
    }
    if (!running.load()) {
        close(sock);
        return;
    }

    // Wait until the logger is started before sending
    while (!startSending.load() && running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));


    auto nextSend = std::chrono::steady_clock::now();
    auto nextPrint = nextSend + std::chrono::seconds(1);
    status::updateRate(0, nextSend);
    uint64_t packet_id = 0;
    uint64_t flow_id = 1;

    // Track current scenario to print a message whenever it changes
    ScenarioPtr lastScenario =
        std::atomic_load_explicit(&gScenario, std::memory_order_acquire);
    std::cout << "[Generator] Scenario " << modeToString(lastScenario->mode)
              << " active" << std::endl;

    status::printStatus();

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

        json j = {
            {"alias", "benchmark"},
            {"source", "benchmark"},
            {"thread_id", 0},
            {"packet_id", packet_id++},
            {"flow_event_id", 1},
            {"flow_event_name", "update"},
            {"flow_id", flow_id},
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

        std::string message_body = j.dump();
        char prefix[6];
        std::snprintf(prefix, sizeof(prefix), "%05zu", message_body.size());
        std::string message = std::string(prefix) + message_body;

        if (send(client, message.data(), message.size(), 0) < 0) {
            std::cerr << "Generator: send() failed, exiting" << std::endl;
            break;
        }

            auto interval = nextInterval(*currentScenario);
            nextSend += interval;
        } else {
            auto us = nextInterval(*currentScenario);
            std::this_thread::sleep_for(us);
        }

        now = std::chrono::steady_clock::now();
        if (now >= nextPrint) {
            status::updateRate(packet_id, now);
            status::printStatus();
            nextPrint += std::chrono::seconds(1);
        }
    }



    close(client);
    close(sock);
}
