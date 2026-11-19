#ifndef ORIGIN_CLIENT_HPP
#define ORIGIN_CLIENT_HPP

#include "http_parser.hpp"
#include <string>

class OriginClient {
private:
    int timeout_seconds_;
    size_t max_response_size_;

public:
    OriginClient(int timeout_seconds = 10, size_t max_response_size = 20 * 1024 * 1024)
        : timeout_seconds_(timeout_seconds), max_response_size_(max_response_size) {}

    HttpResponse forward(const HttpRequest& req, const std::string& extra_headers = "");
    HttpResponse fetch_raw(const std::string& host, int port, const std::string& request_payload);
};

#endif // ORIGIN_CLIENT_HPP
