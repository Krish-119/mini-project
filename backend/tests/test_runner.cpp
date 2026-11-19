#include "http_parser.hpp"
#include "lru_cache.hpp"
#include "lfu_cache.hpp"
#include "cache_freshness.hpp"
#include "cache_manager.hpp"
#include "bounded_queue.hpp"
#include "origin_client.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] Assertion FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

bool test_http_parser() {
    std::cout << "[Test 1] Testing HTTP Request & Response Parser..." << std::endl;

    // Test standard GET request
    std::string raw_req = "GET /index.html?user=alice HTTP/1.1\r\nHost: example.com:8080\r\nUser-Agent: TestClient\r\n\r\n";
    HttpRequest req = HttpParser::parse_request(raw_req);
    TEST_ASSERT(req.is_valid, "Request should be valid");
    TEST_ASSERT(req.method == "GET", "Method must be GET");
    TEST_ASSERT(req.path == "/index.html?user=alice", "Path must match");
    TEST_ASSERT(req.host == "example.com", "Host must be parsed");
    TEST_ASSERT(req.port == 8080, "Port must be 8080");
    TEST_ASSERT(req.query == "user=alice", "Query must match");
    TEST_ASSERT(req.full_cache_key == "http://example.com:8080/index.html?user=alice", "Cache key match");

    // Test response parsing
    std::string raw_res = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 13\r\nCache-Control: max-age=60\r\n\r\nHello, World!";
    HttpResponse res = HttpParser::parse_response(raw_res);
    TEST_ASSERT(res.is_valid, "Response should be valid");
    TEST_ASSERT(res.status_code == 200, "Status must be 200");
    TEST_ASSERT(res.status_text == "OK", "Status text must be OK");
    TEST_ASSERT(res.body == "Hello, World!", "Body must match");
    TEST_ASSERT(res.get_header("content-length") == "13", "Header must match");

    std::cout << "  [PASS] HTTP Parser works correctly." << std::endl;
    return true;
}

bool test_lru_cache() {
    std::cout << "[Test 2] Testing LRU Cache Replacement Policy..." << std::endl;

    LruCache lru;
    CacheEntry e1, e2, e3, out;
    e1.key = "http://site.com/1"; e1.body = "Page 1"; e1.calculate_memory_size();
    e2.key = "http://site.com/2"; e2.body = "Page 2"; e2.calculate_memory_size();
    e3.key = "http://site.com/3"; e3.body = "Page 3"; e3.calculate_memory_size();

    lru.insert(e1.key, e1);
    lru.insert(e2.key, e2);
    lru.insert(e3.key, e3);
    TEST_ASSERT(lru.entry_count() == 3, "LRU entry count should be 3");

    // Access Page 1, moving it to MRU position (e2 is now LRU)
    bool found = lru.lookup(e1.key, out);
    TEST_ASSERT(found && out.body == "Page 1", "Page 1 lookup should succeed");

    // Evict one entry - must evict e2 (least recently used)
    std::string evicted_key;
    size_t freed = 0;
    TEST_ASSERT(lru.evict_one(evicted_key, freed), "Eviction should succeed");
    TEST_ASSERT(evicted_key == e2.key, "LRU must evict Page 2");

    // Now e3 is LRU, e1 is MRU. Evict another - should be e3
    TEST_ASSERT(lru.evict_one(evicted_key, freed), "Eviction should succeed");
    TEST_ASSERT(evicted_key == e3.key, "LRU must evict Page 3 next");

    // Only e1 remains
    TEST_ASSERT(lru.entry_count() == 1, "Only 1 item should remain");
    TEST_ASSERT(lru.lookup(e1.key, out), "Page 1 should still exist");

    std::cout << "  [PASS] LRU Cache eviction order verified." << std::endl;
    return true;
}

bool test_lfu_cache() {
    std::cout << "[Test 3] Testing LFU Cache Replacement Policy..." << std::endl;

    LfuCache lfu;
    CacheEntry e1, e2, e3, out;
    e1.key = "http://site.com/A"; e1.body = "Resource A"; e1.calculate_memory_size();
    e2.key = "http://site.com/B"; e2.body = "Resource B"; e2.calculate_memory_size();
    e3.key = "http://site.com/C"; e3.body = "Resource C"; e3.calculate_memory_size();

    lfu.insert(e1.key, e1);
    lfu.insert(e2.key, e2);
    lfu.insert(e3.key, e3);

    // Access Resource A 3 times (freq = 4)
    lfu.lookup(e1.key, out);
    lfu.lookup(e1.key, out);
    lfu.lookup(e1.key, out);

    // Access Resource C 1 time (freq = 2)
    lfu.lookup(e3.key, out);

    // Resource B has never been accessed after insert (freq = 1)
    // Evict one entry - must evict Resource B!
    std::string evicted_key;
    size_t freed = 0;
    TEST_ASSERT(lfu.evict_one(evicted_key, freed), "Eviction should succeed");
    TEST_ASSERT(evicted_key == e2.key, "LFU must evict least frequently used item (Resource B)");

    // Next eviction should be Resource C (freq = 2 < Resource A freq = 4)
    TEST_ASSERT(lfu.evict_one(evicted_key, freed), "Eviction should succeed");
    TEST_ASSERT(evicted_key == e3.key, "LFU must evict Resource C next");

    // Only Resource A remains
    TEST_ASSERT(lfu.lookup(e1.key, out), "Resource A should still exist");
    // 1 (insert) + 3 (lookups) + 1 (check lookup) = 5 accesses
    TEST_ASSERT(out.access_count == 5, "Resource A frequency should be 5");

    std::cout << "  [PASS] LFU Cache frequency eviction verified." << std::endl;
    return true;
}

bool test_cache_freshness_and_ttl() {
    std::cout << "[Test 4] Testing Cache Freshness, TTL, and Revalidation Eligibility..." << std::endl;

    // Test Cache-Control: max-age=10
    HttpResponse res1;
    res1.status_code = 200;
    res1.set_header("Cache-Control", "public, max-age=10");
    int ttl1 = CacheFreshness::calculate_ttl(res1, 60);
    TEST_ASSERT(ttl1 == 10, "TTL from max-age must be 10");

    // Test Cache-Control: no-store
    HttpResponse res2;
    res2.status_code = 200;
    res2.set_header("Cache-Control", "no-store");
    TEST_ASSERT(!CacheFreshness::is_response_cacheable(res2, 1024 * 1024), "no-store must NOT be cacheable");

    // Test Cache-Control: private
    HttpResponse res3;
    res3.status_code = 200;
    res3.set_header("Cache-Control", "private");
    TEST_ASSERT(!CacheFreshness::is_response_cacheable(res3, 1024 * 1024), "private response must NOT be cached in shared proxy");

    // Test non-200 code (500 Internal Server Error)
    HttpResponse res4;
    res4.status_code = 500;
    TEST_ASSERT(!CacheFreshness::is_response_cacheable(res4, 1024 * 1024), "500 must NOT be cacheable");

    // Test TTL expiration check
    CacheEntry entry;
    entry.key = "http://test.com/exp";
    entry.ttl_seconds = 1; // 1 second TTL
    entry.creation_time = std::chrono::system_clock::now() - std::chrono::seconds(2);
    TEST_ASSERT(entry.is_expired(), "Entry should be detected as expired");

    std::cout << "  [PASS] Cache Freshness rules verified." << std::endl;
    return true;
}

bool test_bounded_queue_backpressure() {
    std::cout << "[Test 5] Testing Bounded Queue & Backpressure Rejection..." << std::endl;

    BoundedQueue<int> queue(3); // Capacity of 3
    TEST_ASSERT(queue.try_push(10, 0), "Push 1 should succeed");
    TEST_ASSERT(queue.try_push(20, 0), "Push 2 should succeed");
    TEST_ASSERT(queue.try_push(30, 0), "Push 3 should succeed");

    // Queue is now full (3 items). A push with 0 timeout must fail immediately
    bool rejected = !queue.try_push(40, 10);
    TEST_ASSERT(rejected, "Push 4 should be rejected due to capacity limit (backpressure)");
    TEST_ASSERT(queue.get_total_rejected() == 1, "Rejected counter should be 1");

    int item = 0;
    TEST_ASSERT(queue.pop(item) && item == 10, "Pop should return item 10");
    TEST_ASSERT(queue.try_push(50, 0), "Push should succeed after pop");

    std::cout << "  [PASS] Bounded Queue & Backpressure Rejection verified." << std::endl;
    return true;
}

bool test_cache_manager_revalidation_and_policy_switch() {
    std::cout << "[Test 6] Testing Cache Manager Revalidation & Dynamic Policy Switch..." << std::endl;

    ProxyConfig cfg;
    cfg.cache_enabled = true;
    cfg.cache_policy = "LRU";
    cfg.max_cache_entries = 100;
    cfg.max_cache_memory_mb = 10;
    cfg.default_ttl_seconds = 60;

    CacheManager mgr(cfg);
    CacheEntry e;
    e.key = "http://api.com/user/10";
    e.status_code = 200;
    e.status_text = "OK";
    e.body = "{\"id\":10,\"name\":\"John\"}";
    e.ttl_seconds = 2; // 2 seconds
    e.creation_time = std::chrono::system_clock::now();
    e.etag = "\"v10\"";

    mgr.insert(e.key, e);
    TEST_ASSERT(mgr.get_entry_count() == 1, "Entry count should be 1");

    CacheEntry out;
    CacheLookupResult res = mgr.lookup(e.key, out);
    TEST_ASSERT(res == CacheLookupResult::HIT, "Should be HIT while fresh");

    // Simulate 304 Not Modified Revalidation
    bool revalidated = mgr.update_revalidated(e.key, 120);
    TEST_ASSERT(revalidated, "update_revalidated should succeed");
    mgr.lookup(e.key, out);
    TEST_ASSERT(out.ttl_seconds == 120, "TTL should be updated to 120");

    // Switch policy to LFU dynamically
    bool switched = mgr.switch_policy("LFU");
    TEST_ASSERT(switched, "Policy switch to LFU should succeed");
    TEST_ASSERT(mgr.get_current_policy_name() == "LFU", "Current policy should be LFU");
    TEST_ASSERT(mgr.get_entry_count() == 1, "Cached entry preserved after policy switch");
    TEST_ASSERT(mgr.lookup(e.key, out) == CacheLookupResult::HIT, "Lookup works after policy switch");

    std::cout << "  [PASS] Cache Manager revalidation & policy switch verified." << std::endl;
    return true;
}

bool test_origin_failure_handling() {
    std::cout << "[Test 7] Testing Origin Failure Resilience (No Crash on Bad Origin)..." << std::endl;

    OriginClient client(1, 1024 * 1024);
    HttpRequest req;
    req.method = "GET";
    req.host = "127.0.0.1";
    req.port = 59999; // Non-existent port
    req.path = "/test";

    // Query non-existent origin
    HttpResponse res = client.forward(req);
    TEST_ASSERT(res.is_valid, "Client must return valid response structure");
    TEST_ASSERT(res.status_code == 502, "Must return 502 Bad Gateway when origin is unreachable");
    TEST_ASSERT(res.body.find("502 Bad Gateway") != std::string::npos, "Body should explain 502 Bad Gateway");

    std::cout << "  [PASS] Origin failure gracefully handled with 502 Bad Gateway." << std::endl;
    return true;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  HTTP Proxy Server with Caching - Unit & System Tests" << std::endl;
    std::cout << "==================================================" << std::endl;

    bool all_passed = true;
    all_passed &= test_http_parser();
    all_passed &= test_lru_cache();
    all_passed &= test_lfu_cache();
    all_passed &= test_cache_freshness_and_ttl();
    all_passed &= test_bounded_queue_backpressure();
    all_passed &= test_cache_manager_revalidation_and_policy_switch();
    all_passed &= test_origin_failure_handling();

    std::cout << "==================================================" << std::endl;
    if (all_passed) {
        std::cout << ">>> ALL 7 TEST SUITES PASSED SUCCESSFULLY! <<<" << std::endl;
        return 0;
    } else {
        std::cerr << ">>> SOME TESTS FAILED! <<<" << std::endl;
        return 1;
    }
}
