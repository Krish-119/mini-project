#include "peer_client.hpp"
#include "origin_client.hpp"
#include <sstream>
#include <iostream>
#include <iomanip>

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
        }
    }
    return escaped.str();
}

bool PeerClient::query_peer_cache(const std::string& key, HttpResponse& out_res) {
    if (!enabled_) return false;

    std::ostringstream req_stream;
    req_stream << "GET /peer/cache?key=" << url_encode(key) << " HTTP/1.1\r\n";
    req_stream << "Host: " << peer_host_ << ":" << peer_port_ << "\r\n";
    req_stream << "X-Proxy-Peer-Hop: 1\r\n";
    req_stream << "Connection: close\r\n\r\n";

    OriginClient client(2, 5 * 1024 * 1024); // fast 2-second timeout for peer query
    HttpResponse res = client.fetch_raw(peer_host_, peer_port_, req_stream.str());

    if (res.is_valid && res.status_code == 200) {
        out_res = res;
        return true;
    }
    return false;
}

bool PeerClient::check_peer_health() {
    if (!enabled_) return false;

    std::string req = "GET /api/health HTTP/1.1\r\nHost: " + peer_host_ + "\r\nConnection: close\r\n\r\n";
    OriginClient client(2, 64 * 1024);
    HttpResponse res = client.fetch_raw(peer_host_, peer_port_, req);
    return res.is_valid && res.status_code == 200;
}
