#include "api_handler.hpp"
#include "metrics.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

static std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%') {
            if (i + 2 < in.size()) {
                int hex_val = 0;
                std::istringstream hex_stream(in.substr(i + 1, 2));
                if (hex_stream >> std::hex >> hex_val) {
                    out += static_cast<char>(hex_val);
                    i += 2;
                } else {
                    out += in[i];
                }
            } else {
                out += in[i];
            }
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string ApiHandler::get_mime_type(const std::string& path) {
    if (path.rfind(".html") != std::string::npos) return "text/html";
    if (path.rfind(".css") != std::string::npos) return "text/css";
    if (path.rfind(".js") != std::string::npos) return "application/javascript";
    if (path.rfind(".json") != std::string::npos) return "application/json";
    if (path.rfind(".png") != std::string::npos) return "image/png";
    if (path.rfind(".jpg") != std::string::npos || path.rfind(".jpeg") != std::string::npos) return "image/jpeg";
    if (path.rfind(".svg") != std::string::npos) return "image/svg+xml";
    if (path.rfind(".ico") != std::string::npos) return "image/x-icon";
    return "text/plain";
}

std::string ApiHandler::read_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool ApiHandler::handle_api_request(const HttpRequest& req, HttpResponse& out_res) {
    out_res.version = "HTTP/1.1";
    out_res.set_header("Access-Control-Allow-Origin", "*");
    out_res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    out_res.set_header("Access-Control-Allow-Headers", "Content-Type, Cache-Control");
    out_res.set_header("Content-Type", "application/json");

    if (req.method == "OPTIONS") {
        out_res.status_code = 204;
        out_res.status_text = "No Content";
        out_res.body = "";
        out_res.set_header("Content-Length", "0");
        out_res.is_valid = true;
        return true;
    }

    // GET /api/health
    if (req.path == "/api/health") {
        out_res.status_code = 200;
        out_res.status_text = "OK";
        std::ostringstream json;
        json << "{\n"
             << "  \"status\": \"healthy\",\n"
             << "  \"uptime_seconds\": " << MetricsEngine::instance().get_uptime_seconds() << ",\n"
             << "  \"version\": \"1.0.0\",\n"
             << "  \"node_id\": \"" << config_.node_id << "\",\n"
             << "  \"policy\": \"" << cache_mgr_.get_current_policy_name() << "\"\n"
             << "}";
        out_res.body = json.str();
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    // GET /api/stats
    if (req.path == "/api/stats") {
        out_res.status_code = 200;
        out_res.status_text = "OK";
        out_res.body = MetricsEngine::instance().to_json_string();
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    // GET /api/cache
    if (req.path == "/api/cache") {
        out_res.status_code = 200;
        out_res.status_text = "OK";
        out_res.body = cache_mgr_.get_entries_json();
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    // GET /api/workers
    if (req.path == "/api/workers") {
        out_res.status_code = 200;
        out_res.status_text = "OK";
        std::ostringstream json;
        json << "{\n"
             << "  \"total_workers\": " << config_.worker_threads << ",\n"
             << "  \"active_workers\": " << (config_.worker_threads - MetricsEngine::instance().to_json_string().find("\"idle_workers\": ") != std::string::npos ? config_.worker_threads : 0) << ",\n"
             << "  \"queue_length\": " << thread_pool_.get_queue_size() << ",\n"
             << "  \"queue_capacity\": " << thread_pool_.get_queue_capacity() << ",\n"
             << "  \"avg_queue_wait_ms\": " << thread_pool_.get_avg_queue_wait_ms() << ",\n"
             << "  \"rejected_tasks\": " << thread_pool_.get_rejected_tasks() << "\n"
             << "}";
        out_res.body = json.str();
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    // POST/GET /api/cache/clear
    if (req.path == "/api/cache/clear") {
        cache_mgr_.clear();
        out_res.status_code = 200;
        out_res.status_text = "OK";
        out_res.body = "{\"status\": \"ok\", \"message\": \"Cache cleared successfully\"}";
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    // POST/GET /api/cache/policy?type=LRU or ?type=LFU
    if (req.path.rfind("/api/cache/policy", 0) == 0) {
        std::string policy = "LRU";
        size_t type_pos = req.query.find("type=");
        if (type_pos != std::string::npos) {
            policy = req.query.substr(type_pos + 5);
            size_t amp = policy.find('&');
            if (amp != std::string::npos) policy = policy.substr(0, amp);
        }

        std::transform(policy.begin(), policy.end(), policy.begin(), ::toupper);
        if (policy == "LRU" || policy == "LFU") {
            cache_mgr_.switch_policy(policy);
            out_res.status_code = 200;
            out_res.status_text = "OK";
            out_res.body = "{\"status\": \"ok\", \"policy\": \"" + policy + "\"}";
        } else {
            out_res.status_code = 400;
            out_res.status_text = "Bad Request";
            out_res.body = "{\"status\": \"error\", \"message\": \"Unsupported policy. Use LRU or LFU.\"}";
        }
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    out_res.status_code = 404;
    out_res.status_text = "Not Found";
    out_res.body = "{\"status\": \"error\", \"message\": \"API endpoint not found\"}";
    out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
    out_res.is_valid = true;
    return true;
}

bool ApiHandler::handle_static_request(const HttpRequest& req, HttpResponse& out_res) {
    std::string rel_path = req.path;
    if (rel_path == "/" || rel_path.empty()) {
        rel_path = "/index.html";
    }

    // Security check: prevent directory traversal
    if (rel_path.find("..") != std::string::npos) {
        out_res.status_code = 403;
        out_res.status_text = "Forbidden";
        out_res.body = "403 Forbidden";
        out_res.set_header("Content-Type", "text/plain");
        out_res.is_valid = true;
        return true;
    }

    std::string filepath = config_.static_dir + rel_path;
    std::string content = read_file(filepath);
    if (content.empty()) {
        content = read_file("../frontend" + rel_path);
    }
    if (content.empty()) {
        content = read_file("./frontend" + rel_path);
    }

    if (content.empty()) {
        out_res.status_code = 404;
        out_res.status_text = "Not Found";
        out_res.body = "404 Not Found: Static resource " + req.path + " not found.";
        out_res.set_header("Content-Type", "text/plain");
        out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
        out_res.is_valid = true;
        return true;
    }

    out_res.status_code = 200;
    out_res.status_text = "OK";
    out_res.body = content;
    out_res.set_header("Content-Type", get_mime_type(rel_path));
    out_res.set_header("Content-Length", std::to_string(content.size()));
    out_res.set_header("Cache-Control", "no-cache");
    out_res.is_valid = true;
    return true;
}

bool ApiHandler::handle_peer_request(const HttpRequest& req, HttpResponse& out_res) {
    // GET /peer/cache?key=<encoded_key>
    size_t key_pos = req.query.find("key=");
    if (key_pos == std::string::npos) {
        out_res.status_code = 400;
        out_res.status_text = "Bad Request";
        out_res.body = "Missing 'key' query parameter";
        out_res.set_header("Content-Type", "text/plain");
        out_res.is_valid = true;
        return true;
    }

    std::string encoded_key = req.query.substr(key_pos + 4);
    size_t amp = encoded_key.find('&');
    if (amp != std::string::npos) encoded_key = encoded_key.substr(0, amp);
    std::string key = url_decode(encoded_key);

    CacheEntry entry;
    CacheLookupResult result = cache_mgr_.lookup(key, entry);

    if (result == CacheLookupResult::HIT) {
        out_res.status_code = entry.status_code;
        out_res.status_text = entry.status_text;
        out_res.raw_headers = entry.raw_headers;
        out_res.headers = entry.headers;
        out_res.body = entry.body;
        out_res.set_header("X-Proxy-Cache", "PEER_HIT");
        out_res.set_header("Content-Length", std::to_string(entry.body.size()));
        out_res.is_valid = true;
        return true;
    }

    out_res.status_code = 404;
    out_res.status_text = "Not Found";
    out_res.body = "Peer Cache Miss";
    out_res.set_header("X-Proxy-Cache", "PEER_MISS");
    out_res.set_header("Content-Type", "text/plain");
    out_res.set_header("Content-Length", std::to_string(out_res.body.size()));
    out_res.is_valid = true;
    return true;
}
