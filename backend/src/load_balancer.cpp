#include "http_parser.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <csignal>

struct BackendNode {
    std::string host;
    int port;
    std::atomic<bool> is_healthy{true};
    std::atomic<uint64_t> total_routed{0};
};

static std::atomic<bool> g_lb_running{true};

void lb_signal_handler(int signum) {
    (void)signum;
    g_lb_running.store(false);
}

class LoadBalancer {
private:
    int listen_port_;
    std::vector<BackendNode*> nodes_;
    std::atomic<size_t> round_robin_idx_{0};
    int listen_sock_ = -1;
    std::thread health_thread_;

    bool check_node_health(BackendNode* node) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(node->port);
        inet_pton(AF_INET, node->host.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return false;
        }

        std::string req = "GET /api/health HTTP/1.1\r\nHost: " + node->host + "\r\nConnection: close\r\n\r\n";
        send(sock, req.c_str(), req.size(), 0);

        char buf[256];
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        close(sock);

        if (n > 0) {
            buf[n] = '\0';
            if (std::string(buf).find("200 OK") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    void health_check_loop() {
        while (g_lb_running.load()) {
            for (auto* node : nodes_) {
                bool healthy = check_node_health(node);
                bool was_healthy = node->is_healthy.exchange(healthy);
                if (was_healthy != healthy) {
                    std::cout << "[LoadBalancer] Node " << node->host << ":" << node->port 
                              << " health changed: " << (healthy ? "HEALTHY" : "DOWN") << std::endl;
                }
            }
            for (int i = 0; i < 20 && g_lb_running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    BackendNode* select_next_node() {
        if (nodes_.empty()) return nullptr;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            size_t idx = round_robin_idx_++ % nodes_.size();
            if (nodes_[idx]->is_healthy.load()) {
                return nodes_[idx];
            }
        }
        return nullptr; // No healthy node available
    }

    void handle_client(int client_fd) {
        BackendNode* target = select_next_node();
        if (!target) {
            std::string err = HttpParser::build_response(502, "Bad Gateway", "text/plain", 
                "502 Bad Gateway: No healthy proxy backend nodes available in load balancer pool.\r\n");
            send(client_fd, err.c_str(), err.size(), 0);
            close(client_fd);
            return;
        }

        target->total_routed++;

        int proxy_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (proxy_sock < 0) {
            close(client_fd);
            return;
        }

        struct sockaddr_in proxy_addr;
        std::memset(&proxy_addr, 0, sizeof(proxy_addr));
        proxy_addr.sin_family = AF_INET;
        proxy_addr.sin_port = htons(target->port);
        inet_pton(AF_INET, target->host.c_str(), &proxy_addr.sin_addr);

        if (connect(proxy_sock, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0) {
            close(proxy_sock);
            target->is_healthy.store(false);
            std::string err = HttpParser::build_response(502, "Bad Gateway", "text/plain", 
                "502 Bad Gateway: Failed to connect to proxy node " + target->host + ":" + std::to_string(target->port) + "\r\n");
            send(client_fd, err.c_str(), err.size(), 0);
            close(client_fd);
            return;
        }

        // Forward request from client to proxy node
        char buf[8192];
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            send(proxy_sock, buf, n, 0);

            // Forward response from proxy node back to client
            while ((n = recv(proxy_sock, buf, sizeof(buf), 0)) > 0) {
                send(client_fd, buf, n, 0);
            }
        }

        close(proxy_sock);
        close(client_fd);
    }

public:
    LoadBalancer(int listen_port, const std::vector<std::pair<std::string, int>>& backends)
        : listen_port_(listen_port) {
        for (const auto& b : backends) {
            auto* node = new BackendNode();
            node->host = b.first;
            node->port = b.second;
            nodes_.push_back(node);
        }
    }

    ~LoadBalancer() {
        stop();
        for (auto* node : nodes_) delete node;
    }

    void stop() {
        g_lb_running.store(false);
        if (listen_sock_ != -1) {
            shutdown(listen_sock_, SHUT_RDWR);
            close(listen_sock_);
            listen_sock_ = -1;
        }
        if (health_thread_.joinable()) {
            health_thread_.join();
        }
    }

    void start() {
        listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(listen_port_);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[LoadBalancer] Failed to bind to port " << listen_port_ << std::endl;
            return;
        }

        if (listen(listen_sock_, 128) < 0) {
            std::cerr << "[LoadBalancer] Failed to listen on port " << listen_port_ << std::endl;
            return;
        }

        std::cout << "==================================================" << std::endl;
        std::cout << "  HTTP Proxy Load Balancer" << std::endl;
        std::cout << "  Listening on: 0.0.0.0:" << listen_port_ << std::endl;
        std::cout << "  Backend Nodes:" << std::endl;
        for (const auto* n : nodes_) {
            std::cout << "   -> " << n->host << ":" << n->port << std::endl;
        }
        std::cout << "==================================================" << std::endl;

        health_thread_ = std::thread(&LoadBalancer::health_check_loop, this);

        while (g_lb_running.load()) {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);
            int client_fd = accept(listen_sock_, (struct sockaddr*)&client_addr, &len);
            if (client_fd < 0) {
                if (!g_lb_running.load()) break;
                continue;
            }

            std::thread(&LoadBalancer::handle_client, this, client_fd).detach();
        }
    }
};

int main(int argc, char* argv[]) {
    int port = 8000;
    std::vector<std::pair<std::string, int>> backends = {
        {"127.0.0.1", 8080},
        {"127.0.0.1", 8081}
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }

    signal(SIGINT, lb_signal_handler);
    signal(SIGTERM, lb_signal_handler);

    LoadBalancer lb(port, backends);
    lb.start();

    return 0;
}
