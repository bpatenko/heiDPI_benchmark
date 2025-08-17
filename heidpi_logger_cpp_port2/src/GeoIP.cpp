#include "GeoIP.hpp"
#include "Logger.hpp"
#include <sstream>

namespace {
nlohmann::json entryToJson(const MMDB_s &db, const MMDB_entry_data_s &entry) {
    switch (entry.type) {
        case MMDB_DATA_TYPE_UTF8_STRING:
            return std::string(entry.utf8_string, entry.data_size);
        case MMDB_DATA_TYPE_DOUBLE:
            return entry.double_value;
        case MMDB_DATA_TYPE_FLOAT:
            return entry.float_value;
        case MMDB_DATA_TYPE_UINT16:
            return entry.uint16;
        case MMDB_DATA_TYPE_UINT32:
            return entry.uint32;
        case MMDB_DATA_TYPE_INT32:
            return entry.int32;
        case MMDB_DATA_TYPE_UINT64:
            return entry.uint64;
        case MMDB_DATA_TYPE_BOOLEAN:
            return static_cast<bool>(entry.boolean);
        case MMDB_DATA_TYPE_MAP: {
            MMDB_entry_s sub{&db, entry.offset};
            MMDB_entry_data_list_s *list = nullptr;
            if (MMDB_get_entry_data_list(&sub, &list) == MMDB_SUCCESS && list) {
                nlohmann::json obj = nlohmann::json::object();
                MMDB_entry_data_list_s *ptr = list;
                while (ptr && ptr->next) {
                    auto key = ptr->entry_data;
                    ptr = ptr->next;
                    auto val = ptr->entry_data;
                    ptr = ptr->next;
                    if (key.type != MMDB_DATA_TYPE_UTF8_STRING) continue;
                    std::string k(key.utf8_string, key.data_size);
                    obj[k] = entryToJson(db, val);
                }
                MMDB_free_entry_data_list(list);
                return obj;
            }
            break;
        }
        case MMDB_DATA_TYPE_ARRAY: {
            MMDB_entry_s sub{&db, entry.offset};
            MMDB_entry_data_list_s *list = nullptr;
            if (MMDB_get_entry_data_list(&sub, &list) == MMDB_SUCCESS && list) {
                nlohmann::json arr = nlohmann::json::array();
                MMDB_entry_data_list_s *ptr = list;
                while (ptr) {
                    arr.push_back(entryToJson(db, ptr->entry_data));
                    ptr = ptr->next;
                }
                MMDB_free_entry_data_list(list);
                return arr;
            }
            break;
        }
        default:
            break;
    }
    return {};
}
} // namespace

GeoIP::GeoIP(const std::string &path, const std::vector<std::string> &k)
    : keys(k) {
    int status = MMDB_open(path.c_str(), MMDB_MODE_MMAP, &mmdb);
    if (status != MMDB_SUCCESS) {
        Logger::error(std::string("GeoIP open failed: ") + path + " " + MMDB_strerror(status));
        loaded = false;
    } else {
        loaded = true;
    }
}

GeoIP::~GeoIP() {
    if (loaded) {
        MMDB_close(&mmdb);
    }
}

nlohmann::json GeoIP::lookup(const std::string &ip) const {
    nlohmann::json result;
    if (!loaded || ip.empty()) return result;
    int gai_error = 0, mmdb_error = 0;
    MMDB_lookup_result_s res = MMDB_lookup_string(&mmdb, ip.c_str(), &gai_error, &mmdb_error);
    if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !res.found_entry) {
        return result;
    }
    for (const auto &key : keys) {
        std::vector<std::string> parts;
        std::stringstream ss(key);
        std::string part;
        while (std::getline(ss, part, '.')) parts.push_back(part);
        std::vector<const char*> path;
        for (auto &p : parts) path.push_back(p.c_str());
        path.push_back(nullptr);
        MMDB_entry_data_s entry{};
        int status = MMDB_aget_value(&res.entry, &entry, path.data());
        if (status != MMDB_SUCCESS || !entry.has_data) continue;
        const std::string &field = parts.back();
        nlohmann::json value = entryToJson(mmdb, entry);
        if (!value.is_null() && !(value.is_object() && value.empty())) {
            result[field] = value;
        }
    }
    return result;
}

void GeoIP::enrich(const std::string &src_ip, const std::string &dst_ip,
                   nlohmann::json &out) const {
    if (!loaded) return;
    auto src = lookup(src_ip);
    if (!src.empty()) {
        out["src_geoip2_city"] = src;
    }
    auto dst = lookup(dst_ip);
    if (!dst.empty()) {
        out["dst_geoip2_city"] = dst;
    }
}