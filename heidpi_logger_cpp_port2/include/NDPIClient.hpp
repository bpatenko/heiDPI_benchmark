#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

/**
 * @brief Simple client for nDPIsrvd server.
 *        Messages are length-prefixed JSON blobs.
 */
class NDPIClient {
public:
    NDPIClient();
    ~NDPIClient();
    void connectTcp(const std::string &host, unsigned short port);
    void connectUnix(const std::string &path);
    void loop(const std::function<void(const nlohmann::json &)> &cb, const std::string &filter="");
    bool read_one(nlohmann::json& out,
                      std::string& eventKey,
                      std::string& eventName,
                      std::string& err);
private:
    int fd{-1};
};

