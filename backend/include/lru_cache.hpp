#ifndef LRU_CACHE_HPP
#define LRU_CACHE_HPP

#include "cache_policy.hpp"
#include <list>
#include <unordered_map>

class LruCache : public ICachePolicy {
private:
    using Item = std::pair<std::string, CacheEntry>;
    std::list<Item> items_;
    std::unordered_map<std::string, std::list<Item>::iterator> map_;

public:
    LruCache() = default;

    CachePolicyType get_type() const override {
        return CachePolicyType::LRU;
    }

    std::string get_type_name() const override {
        return "LRU";
    }

    bool lookup(const std::string& key, CacheEntry& out_entry) override {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }

        // Move to MRU position (front)
        items_.splice(items_.begin(), items_, it->second);
        it->second->second.last_accessed_time = std::chrono::system_clock::now();
        it->second->second.access_count++;
        out_entry = it->second->second;
        return true;
    }

    void insert(const std::string& key, const CacheEntry& entry) override {
        auto it = map_.find(key);
        if (it != map_.end()) {
            // Already present: update content and move to front
            it->second->second = entry;
            it->second->second.last_accessed_time = std::chrono::system_clock::now();
            it->second->second.access_count++;
            items_.splice(items_.begin(), items_, it->second);
        } else {
            // New entry: push to front
            items_.push_front({key, entry});
            map_[key] = items_.begin();
        }
    }

    bool evict_one(std::string& evicted_key, size_t& freed_bytes) override {
        if (items_.empty()) return false;

        auto last = --items_.end();
        evicted_key = last->first;
        freed_bytes = last->second.memory_bytes;

        map_.erase(last->first);
        items_.pop_back();
        return true;
    }

    bool remove(const std::string& key, size_t& freed_bytes) override {
        auto it = map_.find(key);
        if (it == map_.end()) return false;

        freed_bytes = it->second->second.memory_bytes;
        items_.erase(it->second);
        map_.erase(it);
        return true;
    }

    void clear() override {
        items_.clear();
        map_.clear();
    }

    size_t entry_count() const override {
        return map_.size();
    }

    std::vector<CacheEntry> get_all_entries() const override {
        std::vector<CacheEntry> list;
        list.reserve(items_.size());
        for (const auto& item : items_) {
            list.push_back(item.second);
        }
        return list;
    }
};

#endif // LRU_CACHE_HPP
