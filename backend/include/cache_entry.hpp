#ifndef CACHE_ENTRY_HPP
#define CACHE_ENTRY_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

struct CacheEntry {
    std::string key;               // Normalized URL e.g. "http://example.com/foo"
    std::string method = "GET";
    int status_code = 200;
    std::string status_text = "OK";
    std::string version = "HTTP/1.1";

    std::unordered_map<std::string, std::string> headers;
    std::vector<std::pair<std::string, std::string>> raw_headers;
    std::string body;

    // Cache metadata
    size_t memory_bytes = 0;
    std::chrono::system_clock::time_point creation_time;
    std::chrono::system_clock::time_point last_accessed_time;
    int ttl_seconds = 60;
    uint64_t access_count = 1;

    // HTTP Freshness / Revalidation metadata
    std::string etag;
    std::string last_modified;
    bool must_revalidate = false;
    bool is_public = true;

    CacheEntry() = default;

    void calculate_memory_size() {
        size_t bytes = key.size() + body.size() + sizeof(CacheEntry);
        for (const auto& kv : raw_headers) {
            bytes += kv.first.size() + kv.second.size() + 4; // ": \r\n"
        }
        memory_bytes = bytes;
    }

    bool is_expired(std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - creation_time).count();
        return age >= ttl_seconds;
    }

    int get_age_seconds(std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - creation_time).count());
    }

    std::string get_header(const std::string& key_name) const {
        std::string lk = key_name;
        for (auto& c : lk) c = std::tolower(c);
        auto it = headers.find(lk);
        if (it != headers.end()) return it->second;
        return "";
    }
};

#endif // CACHE_ENTRY_HPP
