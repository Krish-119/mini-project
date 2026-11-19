#ifndef PEER_CLIENT_HPP
#define PEER_CLIENT_HPP

#include "http_parser.hpp"
#include <string>

class PeerClient {
private:
    std::string peer_host_;
    int peer_port_;
    bool enabled_;

public:
    PeerClient(const std::string& peer_host, int peer_port, bool enabled)
        : peer_host_(peer_host), peer_port_(peer_port), enabled_(enabled) {}

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    bool query_peer_cache(const std::string& key, HttpResponse& out_res);
    bool check_peer_health();
};

#endif // PEER_CLIENT_HPP
