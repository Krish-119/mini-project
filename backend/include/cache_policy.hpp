#ifndef CACHE_POLICY_HPP
#define CACHE_POLICY_HPP

#include "cache_entry.hpp"
#include <string>
#include <vector>
#include <memory>

enum class CachePolicyType {
    LRU,
    LFU
};

class ICachePolicy {
public:
    virtual ~ICachePolicy() = default;

    virtual CachePolicyType get_type() const = 0;
    virtual std::string get_type_name() const = 0;

    virtual bool lookup(const std::string& key, CacheEntry& out_entry) = 0;
    virtual void insert(const std::string& key, const CacheEntry& entry) = 0;
    virtual bool evict_one(std::string& evicted_key, size_t& freed_bytes) = 0;
    virtual bool remove(const std::string& key, size_t& freed_bytes) = 0;
    virtual void clear() = 0;

    virtual size_t entry_count() const = 0;
    virtual std::vector<CacheEntry> get_all_entries() const = 0;
};

#endif // CACHE_POLICY_HPP
