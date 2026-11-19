# Algorithms

## LRU (Least Recently Used)

The **LRU** policy is implemented with two data structures:
* `std::list<Item>` – a doubly‑linked list where the **front** holds the most‑recently used entry and the **back** holds the least‑recently used entry.
* `std::unordered_map<std::string, std::list<Item>::iterator>` – maps a cache key to its iterator inside the list for **O(1)** lookup.

**Operations**
* **Lookup** – Find the iterator via the hash map, move the entry to the front using `list::splice`, update its `last_accessed_time` and `access_count`.
* **Insert** – If the key already exists, replace the value and move it to the front; otherwise push a new node to the front and store the iterator in the map.
* **Eviction** – Remove the node at the back of the list, erase its key from the map, and free the associated memory.

The algorithm guarantees **O(1)** for all core operations, making it suitable for high‑throughput caching.

---

## LFU (Least Frequently Used)

The **LFU** policy tracks usage frequency using the following structures:
* `key_map_` – maps a cache key to a `LfuNode` which stores the entry, its current `freq` count and an iterator (`freq_iter`) into the frequency list.
* `freq_map_` – maps a frequency value to a `std::list<std::string>` containing all keys that have that exact frequency. The **head** of the list holds the most‑recently accessed key for that frequency.
* `min_freq_` – the smallest frequency currently present in the cache; eviction always starts from this bucket.

**Operations**
* **Lookup** – Retrieve the node, call `touch()` which increments the frequency, moves the key to the next frequency bucket, and updates timestamps.
* **Insert** – Insert a new node with frequency `1` in `freq_map_[1]` and possibly reset `min_freq_` to `1`.
* **Eviction** – Locate the list at `min_freq_`, take the **least recently used** key from the back of that list, remove it from both maps, and update `min_freq_` if the bucket becomes empty.

LFU provides **true frequency‑based eviction**, which can be more effective for workloads where some items are accessed repeatedly.

---

## TTL & Freshness Handling

* **TTL Determination** – The `CacheFreshness::calculate_ttl` function reads `Cache‑Control:max‑age` (or `s‑maxage`) first. If missing, it parses the `Expires` header. If both are absent, it falls back to the proxy's `default_ttl_seconds`.
* **Expiration Check** – `CacheEntry::is_expired` compares the current time with `creation_time + ttl_seconds`.
* **Conditional Revalidation** – When a stale entry is encountered, the proxy sends `If‑None‑Match` (ETag) and/or `If‑Modified‑Since` (Last‑Modified) headers to the origin. If the origin replies `304 Not Modified`, the cached entry's timestamps are refreshed without downloading the body again.

---

## Back‑pressure Queue

The `BoundedQueue<T>` implements a thread‑safe FIFO with a configurable capacity. `try_push` respects a timeout; if the queue is full and the timeout expires, the push fails and the metrics engine records a rejected request. The proxy converts this situation into an HTTP **503 Service Unavailable** response with a `Retry-After` header.
