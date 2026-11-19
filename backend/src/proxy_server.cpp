#include "proxy_server.hpp"
#include "cache_freshness.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>

ProxyServer::ProxyServer(const ProxyConfig& config)
    : config_(config) {
    cache_mgr_ = std::make_unique<CacheManager>(config_);
    thread_pool_ = std::make_unique<ThreadPool>(config_.worker_threads, config_.max_queue_size);
    origin_client_ = std::make_unique<OriginClient>(config_.socket_timeout_seconds, config_.max_response_size_bytes);
    peer_client_ = std::make_unique<PeerClient>(config_.peer_address, config_.peer_port, config_.cluster_enabled);
    api_handler_ = std::make_unique<ApiHandler>(*cache_mgr_, *thread_pool_, config_);
}

ProxyServer::~ProxyServer() {
    stop();
}

void ProxyServer::stop() {
    if (!is_running_.exchange(false)) {
        return;
    }
    std::cout << "[ProxyServer] Initiating graceful shutdown..." << std::endl;
    if (server_sock_ != -1) {
        shutdown(server_sock_, SHUT_RDWR);
        close(server_sock_);
        server_sock_ = -1;
    }
    if (thread_pool_) {
        thread_pool_->shutdown();
    }
    std::cout << "[ProxyServer] Shutdown complete." << std::endl;
}

bool ProxyServer::start() {
    server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_ < 0) {
        std::cerr << "[ProxyServer] Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) <= 0) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(server_sock_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[ProxyServer] Failed to bind to " << config_.bind_address << ":" << config_.port 
                  << " (" << strerror(errno) << ")" << std::endl;
        close(server_sock_);
        server_sock_ = -1;
        return false;
    }

    if (listen(server_sock_, config_.backlog) < 0) {
        std::cerr << "[ProxyServer] Failed to listen on socket: " << strerror(errno) << std::endl;
        close(server_sock_);
        server_sock_ = -1;
        return false;
    }

    is_running_.store(true);
    std::cout << "==================================================" << std::endl;
    std::cout << "  HTTP Proxy Server with Caching [Node: " << config_.node_id << "]" << std::endl;
    std::cout << "  Listening on: " << config_.bind_address << ":" << config_.port << std::endl;
    std::cout << "  Worker Threads: " << config_.worker_threads << " | Queue Capacity: " << config_.max_queue_size << std::endl;
    std::cout << "  Cache: " << (config_.cache_enabled ? "ENABLED (" + config_.cache_policy + ")" : "DISABLED") 
              << " | Memory Limit: " << config_.max_cache_memory_mb << " MB" << std::endl;
    if (config_.cluster_enabled) {
        std::cout << "  Cluster Mode: ENABLED | Peer: " << config_.peer_address << ":" << config_.peer_port << std::endl;
    }
    std::cout << "  Monitoring Dashboard: http://localhost:" << config_.port << "/" << std::endl;
    std::cout << "==================================================" << std::endl;

    while (is_running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_sock_, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (!is_running_.load()) break;
            std::cerr << "[ProxyServer] accept() error: " << strerror(errno) << std::endl;
            continue;
        }

        // Connection Limit Check
        // If active connections exceeds threshold, reject immediately
        // Note: active_connections is tracked via MetricsEngine
        // Submit connection to thread pool with timeout
        bool submitted = thread_pool_->submit([this, client_fd]() {
            handle_client_connection(client_fd);
        }, config_.queue_timeout_ms);

        if (!submitted) {
            send_503_rejection(client_fd, "Proxy request queue capacity reached. Backpressure activated.");
            close(client_fd);
        }
    }

    return true;
}

void ProxyServer::send_503_rejection(int client_fd, const std::string& reason) {
    MetricsEngine::instance().record_rejected_request();
    std::string body = "503 Service Unavailable: " + reason + "\r\n";
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Retry-After", "3"},
        {"X-Proxy-Backpressure", "REJECTED"}
    };
    std::string resp = HttpParser::build_response(503, "Service Unavailable", "text/plain", body, headers);
    send(client_fd, resp.c_str(), resp.size(), 0);
}

void ProxyServer::send_response(int client_fd, const HttpResponse& res, const std::string& cache_status, double latency_ms) {
    HttpResponse final_res = res;
    final_res.set_header("X-Proxy-Cache", cache_status);
    final_res.set_header("X-Proxy-Node", config_.node_id);
    final_res.set_header("X-Response-Time-ms", std::to_string(latency_ms));
    final_res.set_header("Access-Control-Allow-Origin", "*");
    final_res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS, HEAD");
    final_res.set_header("Access-Control-Allow-Headers", "*");

    std::string serialized = final_res.serialize();
    size_t sent = 0;
    while (sent < serialized.size()) {
        ssize_t n = send(client_fd, serialized.c_str() + sent, serialized.size() - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
}

void ProxyServer::handle_client_connection(int client_fd) {
    MetricsEngine::instance().increment_connections();
    auto start_time = std::chrono::steady_clock::now();

    // Set socket timeouts
    struct timeval tv;
    tv.tv_sec = config_.socket_timeout_seconds;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    std::string raw_request;
    char buffer[4096];
    ssize_t bytes_read = 0;

    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        raw_request.append(buffer, bytes_read);
        if (raw_request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
        if (raw_request.size() > config_.max_request_size_bytes) {
            break;
        }
    }

    if (raw_request.empty()) {
        close(client_fd);
        MetricsEngine::instance().decrement_connections();
        return;
    }

    MetricsEngine::instance().record_request();
    HttpRequest req = HttpParser::parse_request(raw_request);

    if (!req.is_valid) {
        HttpResponse bad_req;
        bad_req.status_code = 400;
        bad_req.status_text = "Bad Request";
        bad_req.body = "400 Bad Request: Malformed HTTP request\r\n";
        bad_req.set_header("Content-Type", "text/plain");
        bad_req.set_header("Content-Length", std::to_string(bad_req.body.size()));
        send_response(client_fd, bad_req, "INVALID", 0.1);
        close(client_fd);
        MetricsEngine::instance().decrement_connections();
        return;
    }

    HttpResponse res;
    std::string cache_status = "BYPASS";

    // 1. API Route Handling
    if (req.is_api_request) {
        api_handler_->handle_api_request(req, res);
        cache_status = "API";
    }
    // 2. Peer Cache Route Handling
    else if (req.is_peer_request) {
        api_handler_->handle_peer_request(req, res);
        cache_status = "PEER_ENDPOINT";
    }
    // 3. Static UI Route Handling
    else if (req.is_static_request && (req.host.empty() || req.host == "localhost" || req.host == "127.0.0.1" || req.port == config_.port)) {
        api_handler_->handle_static_request(req, res);
        cache_status = "STATIC";
    }
    // 4. Proxy Forwarding & Caching
    else {
        bool cacheable_req = config_.cache_enabled && CacheFreshness::is_request_cacheable(req);

        if (cacheable_req) {
            CacheEntry cached;
            CacheLookupResult lookup_res = cache_mgr_->lookup(req.full_cache_key, cached);

            if (lookup_res == CacheLookupResult::HIT) {
                // CACHE HIT!
                cache_status = "HIT";
                res.status_code = cached.status_code;
                res.status_text = cached.status_text;
                res.raw_headers = cached.raw_headers;
                res.headers = cached.headers;
                res.body = cached.body;
                res.set_header("Age", std::to_string(cached.get_age_seconds()));
            } else if (lookup_res == CacheLookupResult::STALE) {
                // STALE: Perform conditional revalidation
                std::string reval_headers = CacheFreshness::build_revalidation_headers(cached);
                HttpResponse origin_res = origin_client_->forward(req, reval_headers);

                if (origin_res.status_code == 304) {
                    // Revalidated successfully!
                    int new_ttl = CacheFreshness::calculate_ttl(origin_res, config_.default_ttl_seconds);
                    cache_mgr_->update_revalidated(req.full_cache_key, new_ttl);

                    cache_status = "REVALIDATED";
                    res.status_code = cached.status_code;
                    res.status_text = cached.status_text;
                    res.raw_headers = cached.raw_headers;
                    res.headers = cached.headers;
                    res.body = cached.body;
                } else if (origin_res.status_code == 200) {
                    // Content changed on origin
                    cache_status = "MISS (REFRESHED)";
                    res = origin_res;
                    if (CacheFreshness::is_response_cacheable(origin_res, config_.max_cache_object_size_kb * 1024)) {
                        CacheEntry new_entry;
                        new_entry.key = req.full_cache_key;
                        new_entry.status_code = origin_res.status_code;
                        new_entry.status_text = origin_res.status_text;
                        new_entry.raw_headers = origin_res.raw_headers;
                        new_entry.headers = origin_res.headers;
                        new_entry.body = origin_res.body;
                        new_entry.ttl_seconds = CacheFreshness::calculate_ttl(origin_res, config_.default_ttl_seconds);
                        new_entry.creation_time = std::chrono::system_clock::now();
                        new_entry.last_accessed_time = new_entry.creation_time;
                        new_entry.etag = origin_res.get_header("etag");
                        new_entry.last_modified = origin_res.get_header("last-modified");
                        cache_mgr_->insert(req.full_cache_key, new_entry);
                    }
                } else {
                    res = origin_res;
                    cache_status = "ORIGIN_ERROR";
                }
            } else {
                // CACHE MISS: Check peer if cluster mode enabled and not in a loop
                bool peer_found = false;
                if (config_.cluster_enabled && !req.has_header("x-proxy-peer-hop")) {
                    HttpResponse peer_res;
                    if (peer_client_->query_peer_cache(req.full_cache_key, peer_res)) {
                        res = peer_res;
                        cache_status = "PEER_HIT";
                        peer_found = true;
                    }
                }

                if (!peer_found) {
                    // Forward to Origin Server
                    HttpResponse origin_res = origin_client_->forward(req);
                    res = origin_res;

                    if (CacheFreshness::is_response_cacheable(origin_res, config_.max_cache_object_size_kb * 1024)) {
                        cache_status = "MISS";
                        CacheEntry new_entry;
                        new_entry.key = req.full_cache_key;
                        new_entry.status_code = origin_res.status_code;
                        new_entry.status_text = origin_res.status_text;
                        new_entry.raw_headers = origin_res.raw_headers;
                        new_entry.headers = origin_res.headers;
                        new_entry.body = origin_res.body;
                        new_entry.ttl_seconds = CacheFreshness::calculate_ttl(origin_res, config_.default_ttl_seconds);
                        new_entry.creation_time = std::chrono::system_clock::now();
                        new_entry.last_accessed_time = new_entry.creation_time;
                        new_entry.etag = origin_res.get_header("etag");
                        new_entry.last_modified = origin_res.get_header("last-modified");
                        cache_mgr_->insert(req.full_cache_key, new_entry);
                    } else {
                        cache_status = "BYPASS (UNCACHEABLE)";
                    }
                }
            }
        } else {
            // Not cacheable (POST, PUT, DELETE, or no-store)
            HttpResponse origin_res = origin_client_->forward(req);
            res = origin_res;
            cache_status = "BYPASS";
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double latency_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;
    MetricsEngine::instance().record_latency(latency_ms);

    send_response(client_fd, res, cache_status, latency_ms);
    close(client_fd);
    MetricsEngine::instance().decrement_connections();
}
