#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

struct ProxyConfig {
    int port = 8080;
    std::string bind_address = "0.0.0.0";
    int backlog = 128;

    // Concurrency & Traffic Management
    int worker_threads = 8;
    size_t max_queue_size = 100;
    int queue_timeout_ms = 2000;
    size_t max_concurrent_connections = 250;
    int socket_timeout_seconds = 10;

    // Traffic limits
    size_t max_request_size_bytes = 2 * 1024 * 1024;    // 2 MB
    size_t max_response_size_bytes = 20 * 1024 * 1024;  // 20 MB

    // Cache Configuration
    bool cache_enabled = true;
    std::string cache_policy = "LRU"; // "LRU" or "LFU"
    size_t max_cache_entries = 1000;
    size_t max_cache_memory_mb = 64;
    int default_ttl_seconds = 60;
    size_t max_cache_object_size_kb = 4096; // 4 MB

    // Distributed Caching
    bool cluster_enabled = false;
    std::string node_id = "node-1";
    std::string peer_address = "127.0.0.1";
    int peer_port = 8081;

    // Static Assets
    std::string static_dir = "../frontend";

    static ProxyConfig loadFromFile(const std::string& filepath) {
        ProxyConfig config;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "[Config] Notice: Could not open config file " << filepath 
                      << ". Using default configuration." << std::endl;
            return config;
        }

        std::string line;
        while (std::getline(file, line)) {
            // Trim whitespace
            auto trim = [](std::string& s) {
                s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), s.end());
            };
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            // Remove trailing inline comments
            auto comment_pos = line.find('#');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
                trim(line);
            }

            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = line.substr(0, eq_pos);
            std::string val = line.substr(eq_pos + 1);
            trim(key);
            trim(val);

            try {
                if (key == "port") config.port = std::stoi(val);
                else if (key == "bind_address") config.bind_address = val;
                else if (key == "backlog") config.backlog = std::stoi(val);
                else if (key == "worker_threads") config.worker_threads = std::stoi(val);
                else if (key == "max_queue_size") config.max_queue_size = std::stoul(val);
                else if (key == "queue_timeout_ms") config.queue_timeout_ms = std::stoi(val);
                else if (key == "max_concurrent_connections") config.max_concurrent_connections = std::stoul(val);
                else if (key == "socket_timeout_seconds") config.socket_timeout_seconds = std::stoi(val);
                else if (key == "max_request_size_bytes") config.max_request_size_bytes = std::stoul(val);
                else if (key == "max_response_size_bytes") config.max_response_size_bytes = std::stoul(val);
                else if (key == "cache_enabled") config.cache_enabled = (val == "true" || val == "1");
                else if (key == "cache_policy") config.cache_policy = val;
                else if (key == "max_cache_entries") config.max_cache_entries = std::stoul(val);
                else if (key == "max_cache_memory_mb") config.max_cache_memory_mb = std::stoul(val);
                else if (key == "default_ttl_seconds") config.default_ttl_seconds = std::stoi(val);
                else if (key == "max_cache_object_size_kb") config.max_cache_object_size_kb = std::stoul(val);
                else if (key == "cluster_enabled") config.cluster_enabled = (val == "true" || val == "1");
                else if (key == "node_id") config.node_id = val;
                else if (key == "peer_address") config.peer_address = val;
                else if (key == "peer_port") config.peer_port = std::stoi(val);
                else if (key == "static_dir") config.static_dir = val;
            } catch (...) {
                // Ignore parse errors on individual lines
            }
        }
        return config;
    }
};

#endif // CONFIG_HPP
