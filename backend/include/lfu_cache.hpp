#ifndef LFU_CACHE_HPP
#define LFU_CACHE_HPP

#include "cache_policy.hpp"
#include <unordered_map>
#include <list>

class LfuCache : public ICachePolicy {
private:
    struct LfuNode {
        std::string key;
        CacheEntry entry;
        uint64_t freq = 1;
        std::list<std::string>::iterator freq_iter;
    };

    std::unordered_map<std::string, LfuNode> key_map_;
    std::unordered_map<uint64_t, std::list<std::string>> freq_map_;
    uint64_t min_freq_ = 1;

    void touch(LfuNode& node) {
        uint64_t old_freq = node.freq;
        auto& old_list = freq_map_[old_freq];
        old_list.erase(node.freq_iter);

        if (old_list.empty() && min_freq_ == old_freq) {
            min_freq_++;
        }

        node.freq = old_freq + 1;
        auto& new_list = freq_map_[node.freq];
        new_list.push_front(node.key);
        node.freq_iter = new_list.begin();

        node.entry.access_count = node.freq;
        node.entry.last_accessed_time = std::chrono::system_clock::now();
    }

public:
    LfuCache() : min_freq_(1) {}

    CachePolicyType get_type() const override {
        return CachePolicyType::LFU;
    }

    std::string get_type_name() const override {
        return "LFU";
    }

    bool lookup(const std::string& key, CacheEntry& out_entry) override {
        auto it = key_map_.find(key);
        if (it == key_map_.end()) {
            return false;
        }

        touch(it->second);
        out_entry = it->second.entry;
        return true;
    }

    void insert(const std::string& key, const CacheEntry& entry) override {
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            it->second.entry = entry;
            touch(it->second);
        } else {
            min_freq_ = 1;
            auto& list_one = freq_map_[1];
            list_one.push_front(key);

            LfuNode node;
            node.key = key;
            node.entry = entry;
            node.freq = 1;
            node.freq_iter = list_one.begin();
            node.entry.access_count = 1;
            node.entry.last_accessed_time = std::chrono::system_clock::now();

            key_map_[key] = node;
        }
    }

    bool evict_one(std::string& evicted_key, size_t& freed_bytes) override {
        if (key_map_.empty()) return false;

        while (freq_map_.find(min_freq_) == freq_map_.end() || freq_map_[min_freq_].empty()) {
            min_freq_++;
        }

        auto& victim_list = freq_map_[min_freq_];
        evicted_key = victim_list.back();
        victim_list.pop_back();

        auto it = key_map_.find(evicted_key);
        if (it != key_map_.end()) {
            freed_bytes = it->second.entry.memory_bytes;
            key_map_.erase(it);
            return true;
        }
        return false;
    }

    bool remove(const std::string& key, size_t& freed_bytes) override {
        auto it = key_map_.find(key);
        if (it == key_map_.end()) return false;

        freed_bytes = it->second.entry.memory_bytes;
        freq_map_[it->second.freq].erase(it->second.freq_iter);
        key_map_.erase(it);
        return true;
    }

    void clear() override {
        key_map_.clear();
        freq_map_.clear();
        min_freq_ = 1;
    }

    size_t entry_count() const override {
        return key_map_.size();
    }

    std::vector<CacheEntry> get_all_entries() const override {
        std::vector<CacheEntry> list;
        list.reserve(key_map_.size());
        for (const auto& kv : key_map_) {
            list.push_back(kv.second.entry);
        }
        return list;
    }
};

#endif // LFU_CACHE_HPP
