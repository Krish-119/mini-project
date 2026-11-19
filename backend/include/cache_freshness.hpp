#ifndef CACHE_FRESHNESS_HPP
#define CACHE_FRESHNESS_HPP

#include "http_parser.hpp"
#include "cache_entry.hpp"
#include <string>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>

class CacheFreshness {
public:
    static bool is_request_cacheable(const HttpRequest& req) {
        if (req.method != "GET" && req.method != "HEAD") {
            return false;
        }
        std::string cc = HttpParser::to_lower(req.get_header("cache-control"));
        if (cc.find("no-store") != std::string::npos) {
            return false;
        }
        return true;
    }

    static bool is_response_cacheable(const HttpResponse& res, size_t max_object_size_bytes) {
        // Only 200 OK or 301/304 are cacheable
        if (res.status_code != 200 && res.status_code != 301) {
            return false;
        }

        if (res.body.size() > max_object_size_bytes) {
            return false;
        }

        std::string cc = HttpParser::to_lower(res.get_header("cache-control"));
        // Shared proxy must not cache no-store or private responses
        if (cc.find("no-store") != std::string::npos) {
            return false;
        }
        if (cc.find("private") != std::string::npos) {
            return false;
        }

        // If Pragma: no-cache and no Cache-Control is present
        std::string pragma = HttpParser::to_lower(res.get_header("pragma"));
        if (pragma.find("no-cache") != std::string::npos && cc.empty()) {
            return false;
        }

        return true;
    }

    static int parse_cache_control_max_age(const std::string& cc_header) {
        std::string cc = HttpParser::to_lower(cc_header);

        // Check for s-maxage first (proxy specific RFC 9111)
        size_t s_pos = cc.find("s-maxage=");
        if (s_pos != std::string::npos) {
            size_t val_start = s_pos + 9;
            size_t val_end = cc.find_first_of(",; \t", val_start);
            std::string s_val = cc.substr(val_start, (val_end == std::string::npos) ? std::string::npos : (val_end - val_start));
            try {
                return std::stoi(s_val);
            } catch (...) {}
        }

        // Check for max-age=
        size_t m_pos = cc.find("max-age=");
        if (m_pos != std::string::npos) {
            size_t val_start = m_pos + 8;
            size_t val_end = cc.find_first_of(",; \t", val_start);
            std::string m_val = cc.substr(val_start, (val_end == std::string::npos) ? std::string::npos : (val_end - val_start));
            try {
                return std::stoi(m_val);
            } catch (...) {}
        }

        return -1;
    }

    static time_t parse_http_date(const std::string& date_str) {
        if (date_str.empty()) return 0;
        struct tm tm = {};
        // Try RFC 1123 format: "Fri, 30 Oct 2026 14:19:41 GMT"
        char* res = strptime(date_str.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm);
        if (res != nullptr) {
            return timegm(&tm);
        }
        // Try RFC 850 format: "Friday, 30-Oct-26 14:19:41 GMT"
        res = strptime(date_str.c_str(), "%A, %d-%b-%y %H:%M:%S GMT", &tm);
        if (res != nullptr) {
            return timegm(&tm);
        }
        // Try ANSI C asctime format: "Fri Oct 30 14:19:41 2026"
        res = strptime(date_str.c_str(), "%a %b %d %H:%M:%S %Y", &tm);
        if (res != nullptr) {
            return timegm(&tm);
        }
        return 0;
    }

    static int calculate_ttl(const HttpResponse& res, int default_ttl) {
        std::string cc = res.get_header("cache-control");
        int max_age = parse_cache_control_max_age(cc);
        if (max_age >= 0) {
            return max_age;
        }

        // Try Expires header
        std::string expires = res.get_header("expires");
        if (!expires.empty()) {
            time_t exp_time = parse_http_date(expires);
            if (exp_time > 0) {
                time_t now = time(nullptr);
                if (exp_time > now) {
                    return static_cast<int>(exp_time - now);
                } else {
                    return 0; // Already expired
                }
            }
        }

        return default_ttl;
    }

    static std::string build_revalidation_headers(const CacheEntry& entry) {
        std::ostringstream oss;
        if (!entry.etag.empty()) {
            oss << "If-None-Match: " << entry.etag << "\r\n";
        }
        if (!entry.last_modified.empty()) {
            oss << "If-Modified-Since: " << entry.last_modified << "\r\n";
        }
        return oss.str();
    }
};

#endif // CACHE_FRESHNESS_HPP
