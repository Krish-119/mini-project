#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

struct HttpRequest {
    std::string raw;
    std::string method;
    std::string uri;
    std::string version;

    // Parsed target
    std::string scheme;
    std::string host;
    int port = 80;
    std::string path;
    std::string query;
    std::string full_cache_key; // Normalized full URL

    // Headers map (keys in lowercase)
    std::unordered_map<std::string, std::string> headers;
    // Original case headers for forwarding
    std::vector<std::pair<std::string, std::string>> raw_headers;
    std::string body;

    bool is_valid = false;
    bool is_api_request = false;
    bool is_static_request = false;
    bool is_peer_request = false;

    std::string get_header(const std::string& key) const {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        auto it = headers.find(lower_key);
        if (it != headers.end()) return it->second;
        return "";
    }

    bool has_header(const std::string& key) const {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        return headers.find(lower_key) != headers.end();
    }
};

struct HttpResponse {
    std::string raw;
    std::string version = "HTTP/1.1";
    int status_code = 200;
    std::string status_text = "OK";

    std::unordered_map<std::string, std::string> headers;
    std::vector<std::pair<std::string, std::string>> raw_headers;
    std::string body;

    bool is_valid = false;

    std::string get_header(const std::string& key) const {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        auto it = headers.find(lower_key);
        if (it != headers.end()) return it->second;
        return "";
    }

    void set_header(const std::string& key, const std::string& value) {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        headers[lower_key] = value;
        raw_headers.emplace_back(key, value);
    }

    std::string serialize() const {
        std::ostringstream oss;
        oss << version << " " << status_code << " " << status_text << "\r\n";
        for (const auto& kv : raw_headers) {
            oss << kv.first << ": " << kv.second << "\r\n";
        }
        oss << "\r\n";
        oss << body;
        return oss.str();
    }
};

class HttpParser {
public:
    static HttpRequest parse_request(const std::string& raw_data);
    static HttpResponse parse_response(const std::string& raw_data);
    static std::string build_forward_request(const HttpRequest& req, const std::string& extra_headers = "");
    static std::string build_response(int status_code, const std::string& status_text, 
                                      const std::string& content_type, const std::string& body,
                                      const std::vector<std::pair<std::string, std::string>>& extra_headers = {});

    static std::string trim(const std::string& str);
    static std::string to_lower(const std::string& str);
};

#endif // HTTP_PARSER_HPP
