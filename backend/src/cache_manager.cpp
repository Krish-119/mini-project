#include "cache_manager.hpp"
#include <iostream>
#include <sstream>

CacheManager::CacheManager(const ProxyConfig& config)
    : max_entries_(config.max_cache_entries),
      max_memory_bytes_(config.max_cache_memory_mb * 1024 * 1024),
      max_object_size_bytes_(config.max_cache_object_size_kb * 1024),
      default_ttl_seconds_(config.default_ttl_seconds),
      enabled_(config.cache_enabled) {
    if (config.cache_policy == "LFU") {
        policy_ = std::make_unique<LfuCache>();
    } else {
        policy_ = std::make_unique<LruCache>();
    }
    MetricsEngine::instance().set_current_policy(policy_->get_type_name());
    MetricsEngine::instance().update_cache_storage(0, 0);
}

void CacheManager::enforce_capacity_under_lock(size_t incoming_bytes) {
    while (policy_->entry_count() > 0 && 
          (policy_->entry_count() >= max_entries_ || 
           (current_memory_bytes_ + incoming_bytes > max_memory_bytes_))) {
        std::string evicted_key;
        size_t freed = 0;
        if (policy_->evict_one(evicted_key, freed)) {
            if (freed <= current_memory_bytes_) {
                current_memory_bytes_ -= freed;
            } else {
                current_memory_bytes_ = 0;
            }
            MetricsEngine::instance().record_cache_eviction();
            std::cout << "[Cache] Evicted key: " << evicted_key << " (" << freed << " bytes freed)" << std::endl;
        } else {
            break;
        }
    }
}

CacheLookupResult CacheManager::lookup(const std::string& key, CacheEntry& out_entry) {
    if (!enabled_) {
        return CacheLookupResult::MISS;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!policy_->lookup(key, out_entry)) {
        MetricsEngine::instance().record_cache_miss();
        return CacheLookupResult::MISS;
    }

    if (out_entry.is_expired()) {
        MetricsEngine::instance().record_cache_expired();
        return CacheLookupResult::STALE;
    }

    MetricsEngine::instance().record_cache_hit(out_entry.body.size());
    return CacheLookupResult::HIT;
}

bool CacheManager::insert(const std::string& key, CacheEntry& entry) {
    if (!enabled_) {
        return false;
    }

    entry.calculate_memory_size();
    if (entry.memory_bytes > max_object_size_bytes_) {
        std::cout << "[Cache] Object size " << entry.memory_bytes 
                  << " bytes exceeds max object size limit (" << max_object_size_bytes_ << "). Skipping cache." << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // Remove old version if it existed to accurately balance memory
    size_t old_freed = 0;
    if (policy_->remove(key, old_freed)) {
        if (old_freed <= current_memory_bytes_) {
            current_memory_bytes_ -= old_freed;
        } else {
            current_memory_bytes_ = 0;
        }
    }

    enforce_capacity_under_lock(entry.memory_bytes);

    policy_->insert(key, entry);
    current_memory_bytes_ += entry.memory_bytes;
    MetricsEngine::instance().update_cache_storage(policy_->entry_count(), current_memory_bytes_);
    return true;
}

bool CacheManager::update_revalidated(const std::string& key, int new_ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    CacheEntry entry;
    if (policy_->lookup(key, entry)) {
        entry.creation_time = std::chrono::system_clock::now();
        entry.last_accessed_time = entry.creation_time;
        if (new_ttl > 0) {
            entry.ttl_seconds = new_ttl;
        }
        policy_->insert(key, entry);
        MetricsEngine::instance().record_cache_revalidation();
        return true;
    }
    return false;
}

bool CacheManager::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t freed = 0;
    if (policy_->remove(key, freed)) {
        if (freed <= current_memory_bytes_) {
            current_memory_bytes_ -= freed;
        } else {
            current_memory_bytes_ = 0;
        }
        MetricsEngine::instance().update_cache_storage(policy_->entry_count(), current_memory_bytes_);
        return true;
    }
    return false;
}

void CacheManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_->clear();
    current_memory_bytes_ = 0;
    MetricsEngine::instance().update_cache_storage(0, 0);
}

bool CacheManager::switch_policy(const std::string& policy_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (policy_->get_type_name() == policy_name) {
        return true;
    }

    auto old_entries = policy_->get_all_entries();
    if (policy_name == "LFU") {
        policy_ = std::make_unique<LfuCache>();
    } else if (policy_name == "LRU") {
        policy_ = std::make_unique<LruCache>();
    } else {
        return false;
    }

    for (const auto& entry : old_entries) {
        policy_->insert(entry.key, entry);
    }

    MetricsEngine::instance().set_current_policy(policy_->get_type_name());
    std::cout << "[Cache] Switched policy to " << policy_name << std::endl;
    return true;
}

std::string CacheManager::get_current_policy_name() const {
    return policy_->get_type_name();
}

size_t CacheManager::get_entry_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return policy_->entry_count();
}

size_t CacheManager::get_memory_bytes() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_memory_bytes_;
}

std::string CacheManager::get_entries_json() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = policy_->get_all_entries();
    auto now = std::chrono::system_clock::now();

    std::ostringstream oss;
    oss << "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        oss << "  {\n";
        oss << "    \"key\": \"" << e.key << "\",\n";
        oss << "    \"status_code\": " << e.status_code << ",\n";
        oss << "    \"memory_bytes\": " << e.memory_bytes << ",\n";
        oss << "    \"ttl_seconds\": " << e.ttl_seconds << ",\n";
        oss << "    \"age_seconds\": " << e.get_age_seconds(now) << ",\n";
        oss << "    \"is_expired\": " << (e.is_expired(now) ? "true" : "false") << ",\n";
        oss << "    \"access_count\": " << e.access_count << ",\n";
        oss << "    \"etag\": \"" << e.etag << "\",\n";
        oss << "    \"last_modified\": \"" << e.last_modified << "\"\n";
        oss << "  }" << (i + 1 < entries.size() ? "," : "") << "\n";
    }
    oss << "]";
    return oss.str();
}
