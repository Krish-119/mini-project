#include "proxy_server.hpp"
#include <iostream>
#include <csignal>
#include <memory>

static std::unique_ptr<ProxyServer> g_server;

void signal_handler(int signum) {
    std::cout << "\n[Main] Caught signal " << signum << ". Initiating shutdown..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/proxy.conf";

    ProxyConfig config = ProxyConfig::loadFromFile(config_path);

    // Parse command line overrides
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config_path = argv[++i];
            config = ProxyConfig::loadFromFile(config_path);
        } else if (arg == "-p" && i + 1 < argc) {
            config.port = std::stoi(argv[++i]);
        } else if (arg == "-t" && i + 1 < argc) {
            config.worker_threads = std::stoi(argv[++i]);
        } else if (arg == "--policy" && i + 1 < argc) {
            config.cache_policy = argv[++i];
        } else if (arg == "--cluster") {
            config.cluster_enabled = true;
        } else if (arg == "--node-id" && i + 1 < argc) {
            config.node_id = argv[++i];
        } else if (arg == "--peer-port" && i + 1 < argc) {
            config.peer_port = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  -c <file>          Path to configuration file (default: config/proxy.conf)\n"
                      << "  -p <port>          Port number to listen on (default: 8080)\n"
                      << "  -t <threads>       Worker thread pool count (default: 8)\n"
                      << "  --policy <LRU|LFU> Cache eviction policy\n"
                      << "  --cluster          Enable distributed peer caching\n"
                      << "  --node-id <id>     Unique node ID in cluster\n"
                      << "  --peer-port <port> Peer proxy port\n"
                      << "  -h, --help         Show this help message\n";
            return 0;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_server = std::make_unique<ProxyServer>(config);
    if (!g_server->start()) {
        std::cerr << "[Main] Failed to start proxy server." << std::endl;
        return 1;
    }

    return 0;
}
