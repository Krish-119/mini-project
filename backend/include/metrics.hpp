#ifndef METRICS_HPP
#define METRICS_HPP

#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <string>
#include <sstream>
#include <deque>
#include <algorithm>

struct CacheMetrics {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> revalidations{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> expired_count{0};
    std::atomic<uint64_t> memory_bytes{0};
    std::atomic<size_t> entry_count{0};
    std::atomic<uint64_t> bytes_served{0};
};

class MetricsEngine {
private:
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> origin_requests_{0};
    std::atomic<uint64_t> origin_bytes_{0};
    std::atomic<uint64_t> rejected_requests_503_{0};
    std::atomic<uint64_t> total_errors_{0};

    // Concurrency stats
    std::atomic<size_t> active_connections_{0};
    std::atomic<size_t> active_workers_{0};
    std::atomic<size_t> idle_workers_{0};
    std::atomic<size_t> current_queue_length_{0};
    std::atomic<size_t> queue_capacity_{100};
    std::atomic<double> avg_queue_wait_ms_{0.0};

    // Cache metrics per policy
    CacheMetrics lru_metrics_;
    CacheMetrics lfu_metrics_;
    std::string current_policy_ = "LRU";

    // Latency tracking (sliding window of last 1000 requests)
    mutable std::mutex latency_mutex_;
    std::deque<double> response_times_ms_;
    std::deque<std::chrono::steady_clock::time_point> request_timestamps_;
    const size_t max_samples_ = 1000;

    std::chrono::steady_clock::time_point start_time_;

    MetricsEngine() {
        start_time_ = std::chrono::steady_clock::now();
    }

public:
    static MetricsEngine& instance() {
        static MetricsEngine inst;
        return inst;
    }

    void set_current_policy(const std::string& policy) {
        current_policy_ = policy;
    }

    std::string get_current_policy() const {
        return current_policy_;
    }

    void set_queue_capacity(size_t cap) {
        queue_capacity_.store(cap);
    }

    void set_idle_workers(size_t idle) {
        idle_workers_.store(idle);
    }

    void record_request() {
        total_requests_++;
        std::lock_guard<std::mutex> lock(latency_mutex_);
        auto now = std::chrono::steady_clock::now();
        request_timestamps_.push_back(now);
        // keep timestamps within last 10 seconds for RPS calculation
        while (!request_timestamps_.empty() && 
               std::chrono::duration_cast<std::chrono::seconds>(now - request_timestamps_.front()).count() > 10) {
            request_timestamps_.pop_front();
        }
    }

    void record_origin_request(uint64_t bytes = 0) {
        origin_requests_++;
        origin_bytes_ += bytes;
    }

    void record_cache_hit(uint64_t bytes) {
        if (current_policy_ == "LFU") {
            lfu_metrics_.hits++;
            lfu_metrics_.bytes_served += bytes;
        } else {
            lru_metrics_.hits++;
            lru_metrics_.bytes_served += bytes;
        }
    }

    void record_cache_miss() {
        if (current_policy_ == "LFU") {
            lfu_metrics_.misses++;
        } else {
            lru_metrics_.misses++;
        }
    }

    void record_cache_revalidation() {
        if (current_policy_ == "LFU") {
            lfu_metrics_.revalidations++;
        } else {
            lru_metrics_.revalidations++;
        }
    }

    void record_cache_eviction() {
        if (current_policy_ == "LFU") {
            lfu_metrics_.evictions++;
        } else {
            lru_metrics_.evictions++;
        }
    }

    void record_cache_expired() {
        if (current_policy_ == "LFU") {
            lfu_metrics_.expired_count++;
        } else {
            lru_metrics_.expired_count++;
        }
    }

    void update_cache_storage(size_t entries, uint64_t memory_bytes) {
        if (current_policy_ == "LFU") {
            lfu_metrics_.entry_count.store(entries);
            lfu_metrics_.memory_bytes.store(memory_bytes);
        } else {
            lru_metrics_.entry_count.store(entries);
            lru_metrics_.memory_bytes.store(memory_bytes);
        }
    }

    void record_rejected_request() {
        rejected_requests_503_++;
    }

    void record_error() {
        total_errors_++;
    }

    void increment_connections() {
        active_connections_++;
    }

    void decrement_connections() {
        if (active_connections_ > 0) active_connections_--;
    }

    void increment_active_workers() {
        active_workers_++;
        if (idle_workers_ > 0) idle_workers_--;
    }

    void decrement_active_workers() {
        if (active_workers_ > 0) active_workers_--;
        idle_workers_++;
    }

    void update_queue_state(size_t current_len, double wait_ms) {
        current_queue_length_.store(current_len);
        avg_queue_wait_ms_.store(wait_ms);
    }

    void record_latency(double ms) {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        response_times_ms_.push_back(ms);
        if (response_times_ms_.size() > max_samples_) {
            response_times_ms_.pop_front();
        }
    }

    double get_avg_latency() const {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        if (response_times_ms_.empty()) return 0.0;
        double sum = 0.0;
        for (double d : response_times_ms_) sum += d;
        return sum / response_times_ms_.size();
    }

    double get_p95_latency() const {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        if (response_times_ms_.empty()) return 0.0;
        std::vector<double> sorted(response_times_ms_.begin(), response_times_ms_.end());
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * 0.95);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    double get_throughput_rps() const {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        auto now = std::chrono::steady_clock::now();
        // Count timestamps in the last 5 seconds
        size_t count = 0;
        for (const auto& tp : request_timestamps_) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - tp).count() <= 5) {
                count++;
            }
        }
        return count / 5.0;
    }

    uint64_t get_uptime_seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    }

    std::string to_json_string() const;
};

#endif // METRICS_HPP
