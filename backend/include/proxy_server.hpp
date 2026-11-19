#ifndef PROXY_SERVER_HPP
#define PROXY_SERVER_HPP

#include "config.hpp"
#include "cache_manager.hpp"
#include "thread_pool.hpp"
#include "origin_client.hpp"
#include "peer_client.hpp"
#include "api_handler.hpp"
#include "metrics.hpp"
#include <atomic>
#include <memory>

class ProxyServer {
private:
    ProxyConfig config_;
    std::unique_ptr<CacheManager> cache_mgr_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<OriginClient> origin_client_;
    std::unique_ptr<PeerClient> peer_client_;
    std::unique_ptr<ApiHandler> api_handler_;

    int server_sock_ = -1;
    std::atomic<bool> is_running_{false};

    void handle_client_connection(int client_fd);
    void send_503_rejection(int client_fd, const std::string& reason);
    void send_response(int client_fd, const HttpResponse& res, const std::string& cache_status, double latency_ms);

public:
    explicit ProxyServer(const ProxyConfig& config);
    ~ProxyServer();

    bool start();
    void stop();
    bool is_running() const { return is_running_.load(); }
};

#endif // PROXY_SERVER_HPP
