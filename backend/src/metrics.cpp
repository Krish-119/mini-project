#include "metrics.hpp"
#include <iomanip>
#include <sstream>

std::string MetricsEngine::to_json_string() const {
    uint64_t total = total_requests_.load();
    const CacheMetrics& cur_cm = (current_policy_ == "LFU") ? lfu_metrics_ : lru_metrics_;

    uint64_t hits = cur_cm.hits.load();
    uint64_t misses = cur_cm.misses.load();
    uint64_t revals = cur_cm.revalidations.load();
    uint64_t evicts = cur_cm.evictions.load();
    uint64_t expired = cur_cm.expired_count.load();
    uint64_t mem_bytes = cur_cm.memory_bytes.load();
    size_t entries = cur_cm.entry_count.load();
    uint64_t bytes_served = cur_cm.bytes_served.load();

    uint64_t cache_attempts = hits + misses;
    double hit_ratio = (cache_attempts > 0) ? (static_cast<double>(hits) / cache_attempts) * 100.0 : 0.0;

    // LRU stats
    uint64_t lru_h = lru_metrics_.hits.load();
    uint64_t lru_m = lru_metrics_.misses.load();
    double lru_ratio = (lru_h + lru_m > 0) ? (static_cast<double>(lru_h) / (lru_h + lru_m)) * 100.0 : 0.0;

    // LFU stats
    uint64_t lfu_h = lfu_metrics_.hits.load();
    uint64_t lfu_m = lfu_metrics_.misses.load();
    double lfu_ratio = (lfu_h + lfu_m > 0) ? (static_cast<double>(lfu_h) / (lfu_h + lfu_m)) * 100.0 : 0.0;

    uint64_t orig_req = origin_requests_.load();
    uint64_t rej_503 = rejected_requests_503_.load();
    uint64_t errs = total_errors_.load();
    double err_rate = (total > 0) ? (static_cast<double>(errs + rej_503) / total) * 100.0 : 0.0;

    double avg_latency = get_avg_latency();
    double p95_latency = get_p95_latency();
    double rps = get_throughput_rps();

    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    json << "{\n";
    json << "  \"status\": \"healthy\",\n";
    json << "  \"uptime_seconds\": " << get_uptime_seconds() << ",\n";
    json << "  \"policy\": \"" << current_policy_ << "\",\n";
    json << "  \"total_requests\": " << total << ",\n";
    json << "  \"origin_requests\": " << orig_req << ",\n";
    json << "  \"origin_avoided_requests\": " << hits << ",\n";
    json << "  \"cache_hits\": " << hits << ",\n";
    json << "  \"cache_misses\": " << misses << ",\n";
    json << "  \"cache_revalidations\": " << revals << ",\n";
    json << "  \"cache_hit_ratio_percent\": " << hit_ratio << ",\n";
    json << "  \"cache_memory_bytes\": " << mem_bytes << ",\n";
    json << "  \"cache_memory_mb\": " << (static_cast<double>(mem_bytes) / (1024.0 * 1024.0)) << ",\n";
    json << "  \"cache_entries\": " << entries << ",\n";
    json << "  \"cache_evictions\": " << evicts << ",\n";
    json << "  \"cache_expired_count\": " << expired << ",\n";
    json << "  \"cache_bytes_served\": " << bytes_served << ",\n";
    json << "  \"active_connections\": " << active_connections_.load() << ",\n";
    json << "  \"active_workers\": " << active_workers_.load() << ",\n";
    json << "  \"idle_workers\": " << idle_workers_.load() << ",\n";
    json << "  \"queue_length\": " << current_queue_length_.load() << ",\n";
    json << "  \"queue_capacity\": " << queue_capacity_.load() << ",\n";
    json << "  \"avg_queue_wait_ms\": " << avg_queue_wait_ms_.load() << ",\n";
    json << "  \"rejected_requests_503\": " << rej_503 << ",\n";
    json << "  \"total_errors\": " << errs << ",\n";
    json << "  \"error_rate_percent\": " << err_rate << ",\n";
    json << "  \"avg_response_time_ms\": " << avg_latency << ",\n";
    json << "  \"p95_response_time_ms\": " << p95_latency << ",\n";
    json << "  \"throughput_rps\": " << rps << ",\n";
    json << "  \"lru_performance\": {\n";
    json << "    \"hits\": " << lru_h << ",\n";
    json << "    \"misses\": " << lru_m << ",\n";
    json << "    \"hit_ratio_percent\": " << lru_ratio << ",\n";
    json << "    \"evictions\": " << lru_metrics_.evictions.load() << "\n";
    json << "  },\n";
    json << "  \"lfu_performance\": {\n";
    json << "    \"hits\": " << lfu_h << ",\n";
    json << "    \"misses\": " << lfu_m << ",\n";
    json << "    \"hit_ratio_percent\": " << lfu_ratio << ",\n";
    json << "    \"evictions\": " << lfu_metrics_.evictions.load() << "\n";
    json << "  }\n";
    json << "}";

    return json.str();
}
