#ifndef API_HANDLER_HPP
#define API_HANDLER_HPP

#include "http_parser.hpp"
#include "cache_manager.hpp"
#include "thread_pool.hpp"
#include "config.hpp"
#include <string>

class ApiHandler {
private:
    CacheManager& cache_mgr_;
    ThreadPool& thread_pool_;
    const ProxyConfig& config_;

    std::string get_mime_type(const std::string& path);
    std::string read_file(const std::string& filepath);

public:
    ApiHandler(CacheManager& cache_mgr, ThreadPool& thread_pool, const ProxyConfig& config)
        : cache_mgr_(cache_mgr), thread_pool_(thread_pool), config_(config) {}

    bool handle_api_request(const HttpRequest& req, HttpResponse& out_res);
    bool handle_static_request(const HttpRequest& req, HttpResponse& out_res);
    bool handle_peer_request(const HttpRequest& req, HttpResponse& out_res);
};

#endif // API_HANDLER_HPP
