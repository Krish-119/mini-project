#ifndef CACHE_MANAGER_HPP
#define CACHE_MANAGER_HPP

#include "cache_entry.hpp"
#include "cache_policy.hpp"
#include "lru_cache.hpp"
#include "lfu_cache.hpp"
#include "config.hpp"
#include "metrics.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class CacheLookupResult {
    HIT,
    MISS,
    STALE
};

class CacheManager {
private:
    std::unique_ptr<ICachePolicy> policy_;
    std::mutex mutex_;

    size_t max_entries_;
    size_t max_memory_bytes_;
    size_t max_object_size_bytes_;
    int default_ttl_seconds_;
    size_t current_memory_bytes_ = 0;
    bool enabled_ = true;

    void enforce_capacity_under_lock(size_t incoming_bytes);

public:
    explicit CacheManager(const ProxyConfig& config);

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    CacheLookupResult lookup(const std::string& key, CacheEntry& out_entry);
    bool insert(const std::string& key, CacheEntry& entry);
    bool update_revalidated(const std::string& key, int new_ttl);
    bool remove(const std::string& key);
    void clear();

    bool switch_policy(const std::string& policy_name);
    std::string get_current_policy_name() const;

    size_t get_entry_count();
    size_t get_memory_bytes();

    std::string get_entries_json();
};

#endif // CACHE_MANAGER_HPP
