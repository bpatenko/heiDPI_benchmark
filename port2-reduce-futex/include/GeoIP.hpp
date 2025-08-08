#pragma once
#include <string>
#include <nlohmann/json.hpp>

/**
 * @brief Stub for GeoIP lookup. Currently a no-op.
 */
class GeoIP {
public:
    void enrich(const nlohmann::json&, nlohmann::json&) const { /* no-op */ }
};

