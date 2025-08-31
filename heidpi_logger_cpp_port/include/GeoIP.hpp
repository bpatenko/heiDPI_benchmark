#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <maxminddb.h>

/**
 * @brief Performs GeoIP lookups using a MaxMind DB and enriches events.
 */
class GeoIP {
public:
    GeoIP() = default;
    GeoIP(const std::string &path, const std::vector<std::string> &keys);
    ~GeoIP();

    void enrich(const std::string &src_ip, const std::string &dst_ip,
                nlohmann::json &out) const;

private:
    nlohmann::json lookup(const std::string &ip) const;

    MMDB_s mmdb{};
    bool loaded{false};
    std::vector<std::string> keys;
};

