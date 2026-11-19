#include "http_parser.hpp"
#include <iostream>

std::string HttpParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string HttpParser::to_lower(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

HttpRequest HttpParser::parse_request(const std::string& raw_data) {
    HttpRequest req;
    req.raw = raw_data;

    size_t header_end = raw_data.find("\r\n\r\n");
    std::string header_part = (header_end != std::string::npos) ? raw_data.substr(0, header_end) : raw_data;
    if (header_end != std::string::npos) {
        req.body = raw_data.substr(header_end + 4);
    }

    std::istringstream stream(header_part);
    std::string request_line;
    if (!std::getline(stream, request_line)) {
        req.is_valid = false;
        return req;
    }

    // Strip carriage return
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    // Parse request line: METHOD URI VERSION
    std::istringstream line_stream(request_line);
    if (!(line_stream >> req.method >> req.uri >> req.version)) {
        req.is_valid = false;
        return req;
    }

    // Parse headers
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));
            std::string lower_key = to_lower(key);

            req.headers[lower_key] = value;
            req.raw_headers.emplace_back(key, value);
        }
    }

    // Determine Host, Port, Path, and Scheme
    req.scheme = "http";
    req.port = 80;

    // Check if URI is absolute
    std::string uri = req.uri;
    if (uri.rfind("http://", 0) == 0) {
        std::string without_scheme = uri.substr(7);
        size_t slash_pos = without_scheme.find('/');
        std::string host_port = (slash_pos != std::string::npos) ? without_scheme.substr(0, slash_pos) : without_scheme;
        req.path = (slash_pos != std::string::npos) ? without_scheme.substr(slash_pos) : "/";

        size_t colon_pos = host_port.find(':');
        if (colon_pos != std::string::npos) {
            req.host = host_port.substr(0, colon_pos);
            try {
                req.port = std::stoi(host_port.substr(colon_pos + 1));
            } catch (...) {
                req.port = 80;
            }
        } else {
            req.host = host_port;
            req.port = 80;
        }
    } else {
        // Relative URI
        req.path = uri;
        std::string host_header = req.get_header("host");
        if (!host_header.empty()) {
            size_t colon_pos = host_header.find(':');
            if (colon_pos != std::string::npos) {
                req.host = host_header.substr(0, colon_pos);
                try {
                    req.port = std::stoi(host_header.substr(colon_pos + 1));
                } catch (...) {
                    req.port = 80;
                }
            } else {
                req.host = host_header;
                req.port = 80;
            }
        }
    }

    if (req.path.empty()) req.path = "/";

    // Split query string if any
    size_t qmark = req.path.find('?');
    if (qmark != std::string::npos) {
        req.query = req.path.substr(qmark + 1);
    }

    // Construct full cache key
    if (!req.host.empty()) {
        std::ostringstream key_stream;
        key_stream << req.scheme << "://" << req.host;
        if (req.port != 80 && req.port != 443) {
            key_stream << ":" << req.port;
        }
        key_stream << req.path;
        req.full_cache_key = key_stream.str();
    } else {
        req.full_cache_key = req.path;
    }

    // Route classifications
    if (req.path.rfind("/api/", 0) == 0) {
        req.is_api_request = true;
    } else if (req.path.rfind("/peer/", 0) == 0) {
        req.is_peer_request = true;
    } else if (req.path == "/" || req.path == "/index.html" || 
               req.path.rfind("/css/", 0) == 0 || req.path.rfind("/js/", 0) == 0 ||
               req.path.rfind("/assets/", 0) == 0) {
        req.is_static_request = true;
    }

    req.is_valid = true;
    return req;
}

HttpResponse HttpParser::parse_response(const std::string& raw_data) {
    HttpResponse res;
    res.raw = raw_data;

    size_t header_end = raw_data.find("\r\n\r\n");
    std::string header_part = (header_end != std::string::npos) ? raw_data.substr(0, header_end) : raw_data;
    if (header_end != std::string::npos) {
        res.body = raw_data.substr(header_end + 4);
    }

    std::istringstream stream(header_part);
    std::string status_line;
    if (!std::getline(stream, status_line)) {
        res.is_valid = false;
        return res;
    }

    if (!status_line.empty() && status_line.back() == '\r') {
        status_line.pop_back();
    }

    // Status line: HTTP/1.1 200 OK
    size_t first_space = status_line.find(' ');
    if (first_space != std::string::npos) {
        res.version = status_line.substr(0, first_space);
        size_t second_space = status_line.find(' ', first_space + 1);
        if (second_space != std::string::npos) {
            try {
                res.status_code = std::stoi(status_line.substr(first_space + 1, second_space - first_space - 1));
            } catch (...) {
                res.status_code = 0;
            }
            res.status_text = status_line.substr(second_space + 1);
        } else {
            try {
                res.status_code = std::stoi(status_line.substr(first_space + 1));
            } catch (...) {
                res.status_code = 0;
            }
        }
    }

    // Parse headers
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));
            std::string lower_key = to_lower(key);

            res.headers[lower_key] = value;
            res.raw_headers.emplace_back(key, value);
        }
    }

    // Handle chunked transfer decoding if necessary
    std::string te = res.get_header("transfer-encoding");
    if (to_lower(te).find("chunked") != std::string::npos) {
        std::string decoded_body;
        size_t pos = 0;
        while (pos < res.body.size()) {
            size_t crlf = res.body.find("\r\n", pos);
            if (crlf == std::string::npos) break;
            std::string chunk_size_str = res.body.substr(pos, crlf - pos);
            size_t chunk_size = 0;
            try {
                chunk_size = std::stoul(chunk_size_str, nullptr, 16);
            } catch (...) {
                break;
            }
            if (chunk_size == 0) break;

            pos = crlf + 2;
            if (pos + chunk_size <= res.body.size()) {
                decoded_body.append(res.body, pos, chunk_size);
                pos += chunk_size + 2; // skip chunk + \r\n
            } else {
                decoded_body.append(res.body.substr(pos));
                break;
            }
        }
        res.body = decoded_body;
    }

    res.is_valid = (res.status_code > 0);
    return res;
}

std::string HttpParser::build_forward_request(const HttpRequest& req, const std::string& extra_headers) {
    std::ostringstream oss;
    oss << req.method << " " << req.path << " HTTP/1.1\r\n";
    oss << "Host: " << req.host;
    if (req.port != 80 && req.port != 443) {
        oss << ":" << req.port;
    }
    oss << "\r\n";

    for (const auto& kv : req.raw_headers) {
        std::string lk = to_lower(kv.first);
        // Exclude hop-by-hop headers and host
        if (lk == "host" || lk == "connection" || lk == "proxy-connection" || lk == "keep-alive") {
            continue;
        }
        oss << kv.first << ": " << kv.second << "\r\n";
    }

    oss << "Connection: close\r\n";
    if (!extra_headers.empty()) {
        oss << extra_headers;
    }
    oss << "\r\n";
    oss << req.body;
    return oss.str();
}

std::string HttpParser::build_response(int status_code, const std::string& status_text, 
                                      const std::string& content_type, const std::string& body,
                                      const std::vector<std::pair<std::string, std::string>>& extra_headers) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS, HEAD\r\n";
    oss << "Access-Control-Allow-Headers: Content-Type, Cache-Control, If-None-Match, If-Modified-Since\r\n";

    for (const auto& h : extra_headers) {
        oss << h.first << ": " << h.second << "\r\n";
    }
    oss << "\r\n";
    oss << body;
    return oss.str();
}
