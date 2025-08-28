#include "NDPIClient.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>

NDPIClient::NDPIClient() {}
NDPIClient::~NDPIClient() { if (fd >= 0) ::close(fd); }

void NDPIClient::connectTcp(const std::string &host, unsigned short port) {
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("connect");
}

void NDPIClient::connectUnix(const std::string &path) {
    fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket");
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("connect");
}

void NDPIClient::loop(const std::function<void(const nlohmann::json &)> &cb, const std::string &filter) {
    // send optional filter expression before starting the receive loop
    if (!filter.empty()) {
        std::ostringstream ss;
        ss << std::setw(6) << std::setfill('0') << filter.size() << filter;
        std::string msg = ss.str();
        ssize_t sent = ::send(fd, msg.c_str(), msg.size(), 0);
        if (sent < 0 || static_cast<size_t>(sent) != msg.size())
            throw std::runtime_error("send");
    }

    while (true) {
        char lenbuf[6];
        // Der Generator verwendet immer fünf Ziffern für die Länge
        ssize_t n = ::recv(fd, lenbuf, 5, MSG_WAITALL);
        if (n <= 0) break;
        lenbuf[5] = '\0';
        size_t len = std::stoul(lenbuf);
        // anschließend die JSON‑Nutzlast lesen (inklusive '{')
        std::string payload(len, '\0');
        n = ::recv(fd, payload.data(), len, MSG_WAITALL);
        if (n <= 0) break;
        try {
            auto j = nlohmann::json::parse(payload);
            cb(j);
        } catch (...) {
            // JSON‑Fehler ignorieren
        }
    }
}

bool NDPIClient::read_one(nlohmann::json& out,
                          std::string& key,
                          std::string& name,
                          std::string& err) {
    key.clear(); name.clear(); err.clear();

    char lenbuf[6];
    ssize_t n = ::recv(fd, lenbuf, 5, MSG_WAITALL);
    if (n == 0) { err = "eof"; return false; }
    if (n <  0) { err = "recv len"; return false; }
    lenbuf[5] = '\0';

    size_t len = 0;
    try { len = std::stoul(lenbuf); } catch (...) { err = "bad length"; return false; }

    std::string payload(len, '\0');
    n = ::recv(fd, payload.data(), len, MSG_WAITALL);
    if (n <= 0) { err = "payload recv"; return false; }

    try { out = nlohmann::json::parse(payload); }
    catch (...) { err = "json parse"; return false; }

    if (out.contains("flow_event_name"))      { key = "flow_event_name";   name = out["flow_event_name"]; }
    else if (out.contains("packet_event_name")){ key = "packet_event_name"; name = out["packet_event_name"]; }
    else if (out.contains("daemon_event_name")){ key = "daemon_event_name"; name = out["daemon_event_name"]; }
    else if (out.contains("error_event_name")) { key = "error_event_name";  name = out["error_event_name"]; }

    return true;
}

