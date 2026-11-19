#include "origin_client.hpp"
#include "metrics.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <cstring>
#include <iostream>

HttpResponse OriginClient::forward(const HttpRequest& req, const std::string& extra_headers) {
    std::string payload = HttpParser::build_forward_request(req, extra_headers);
    return fetch_raw(req.host, req.port, payload);
}

HttpResponse OriginClient::fetch_raw(const std::string& host, int port, const std::string& request_payload) {
    HttpResponse res;

    struct addrinfo hints;
    struct addrinfo *servinfo = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int status = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &servinfo);
    if (status != 0) {
        std::cerr << "[OriginClient] DNS lookup failed for " << host << ": " << gai_strerror(status) << std::endl;
        MetricsEngine::instance().record_error();
        res.status_code = 502;
        res.status_text = "Bad Gateway";
        res.body = "502 Bad Gateway: Could not resolve origin host " + host;
        res.set_header("Content-Type", "text/plain");
        res.set_header("Content-Length", std::to_string(res.body.size()));
        res.is_valid = true;
        return res;
    }

    int sock = -1;
    struct addrinfo *p = nullptr;
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == -1) continue;

        // Set socket send and receive timeouts
        struct timeval tv;
        tv.tv_sec = timeout_seconds_;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        if (connect(sock, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock);
            sock = -1;
            continue;
        }
        break; // Successfully connected
    }

    freeaddrinfo(servinfo);

    if (sock == -1) {
        std::cerr << "[OriginClient] Connection failed to " << host << ":" << port << std::endl;
        MetricsEngine::instance().record_error();
        res.status_code = 502;
        res.status_text = "Bad Gateway";
        res.body = "502 Bad Gateway: Connection refused or failed to origin server " + host + ":" + port_str;
        res.set_header("Content-Type", "text/plain");
        res.set_header("Content-Length", std::to_string(res.body.size()));
        res.is_valid = true;
        return res;
    }

    // Send HTTP request to origin
    size_t total_sent = 0;
    const char* ptr = request_payload.c_str();
    size_t to_send = request_payload.size();

    while (total_sent < to_send) {
        ssize_t bytes = send(sock, ptr + total_sent, to_send - total_sent, 0);
        if (bytes <= 0) {
            std::cerr << "[OriginClient] Error sending to origin " << host << std::endl;
            close(sock);
            MetricsEngine::instance().record_error();
            res.status_code = 504;
            res.status_text = "Gateway Timeout";
            res.body = "504 Gateway Timeout: Error sending request to origin";
            res.set_header("Content-Type", "text/plain");
            res.is_valid = true;
            return res;
        }
        total_sent += bytes;
    }

    // Read response from origin
    std::string raw_response;
    char buffer[8192];
    bool read_error = false;
    ssize_t bytes_read = 0;

    size_t expected_content_len = 0;
    bool content_len_found = false;
    size_t header_len = 0;

    while ((bytes_read = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        raw_response.append(buffer, bytes_read);

        if (raw_response.size() > max_response_size_) {
            std::cerr << "[OriginClient] Origin response exceeded max allowed size: " << max_response_size_ << std::endl;
            read_error = true;
            break;
        }

        // Check if we parsed headers and know Content-Length
        if (!content_len_found) {
            size_t delim = raw_response.find("\r\n\r\n");
            if (delim != std::string::npos) {
                header_len = delim + 4;
                // Look for Content-Length
                std::string lower_headers = HttpParser::to_lower(raw_response.substr(0, header_len));
                size_t cl_pos = lower_headers.find("content-length:");
                if (cl_pos != std::string::npos) {
                    size_t end_line = lower_headers.find("\r\n", cl_pos);
                    std::string cl_str = HttpParser::trim(lower_headers.substr(cl_pos + 15, end_line - cl_pos - 15));
                    try {
                        expected_content_len = std::stoul(cl_str);
                        content_len_found = true;
                    } catch (...) {}
                }
            }
        }

        if (content_len_found && (raw_response.size() >= header_len + expected_content_len)) {
            break; // Finished receiving full response body
        }
    }

    close(sock);

    if (read_error) {
        MetricsEngine::instance().record_error();
        res.status_code = 502;
        res.status_text = "Bad Gateway";
        res.body = "502 Bad Gateway: Origin response too large";
        res.set_header("Content-Type", "text/plain");
        res.is_valid = true;
        return res;
    }

    if (raw_response.empty()) {
        std::cerr << "[OriginClient] Empty response from origin" << std::endl;
        MetricsEngine::instance().record_error();
        res.status_code = 504;
        res.status_text = "Gateway Timeout";
        res.body = "504 Gateway Timeout: Empty response from origin";
        res.set_header("Content-Type", "text/plain");
        res.is_valid = true;
        return res;
    }

    MetricsEngine::instance().record_origin_request(raw_response.size());
    return HttpParser::parse_response(raw_response);
}
