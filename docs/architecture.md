# HTTP Proxy Server with Caching

## Overview

This project implements a fully functional **HTTP Proxy Server** with **in‑memory caching**. It supports:

* HTTP/1.1 request parsing and forwarding to origin servers.
* Configurable **LRU** or **LFU** eviction policies with **TTL** handling.
* Cache **freshness** validation using `Cache‑Control`, `ETag`, and `If‑Modified‑Since`.
* Multithreaded request processing via a fixed‑size **worker thread pool** and a **bounded request queue** that provides back‑pressure (HTTP 503 when the queue is full).
* Monitoring **API** (`/api/health`, `/api/stats`, `/api/cache`, `/api/workers`).
* Interactive **mini‑browser** and a rich **dashboard** (Chart.js) implemented in plain HTML/CSS/JS.
* Optional **distributed caching** via peer nodes (cluster mode).

The code is written in **C++17** for the backend and vanilla web technologies for the frontend.

---

## Architecture (docs/architecture.md)

* **ProxyServer** – Core server that accepts client connections, parses requests, decides whether to serve from cache, query a peer, or forward to an origin server.
* **CacheManager** – Holds the selected `ICachePolicy` implementation (`LruCache` or `LfuCache`). Handles lookup, insert, eviction, revalidation, and statistics.
* **OriginClient** – Handles low‑level TCP communication with the origin server (including time‑outs and max response size).
* **PeerClient** – Optional component for querying a remote proxy node's cache.
* **ThreadPool** – Fixed number of worker threads consuming tasks from a `BoundedQueue`. Queue capacity and timeout are configurable.
* **MetricsEngine** – Global singleton that records request counts, cache hits/misses, latency, queue statistics, etc., and produces JSON for the monitoring API.
* **ApiHandler** – Serves the monitoring endpoints and static UI assets.
* **LoadBalancer** (optional) – Small utility to distribute traffic across multiple proxy instances (see `backend/src/load_balancer.cpp`).

---

## Algorithms (docs/algorithms.md)

### LRU (Least Recently Used)
* Implemented using a **doubly‑linked list** (`std::list`) and a **hash map** (`std::unordered_map`) mapping keys to list iterators.
* On lookup the element is moved to the front (MRU). Eviction removes the element at the back (LRU).

### LFU (Least Frequently Used)
* Maintains a **frequency map** (`freq_map_`) that groups keys by access count.
* Each cache entry stores its frequency and an iterator into the corresponding frequency list.
* On access the entry's frequency is incremented and moved to the next frequency list. Eviction removes an entry from the smallest frequency list (`min_freq_`).

### TTL & Freshness
* When inserting a response the TTL is derived from `Cache‑Control:max‑age`, `Expires` header, or a configurable default.
* `CacheEntry::is_expired()` checks if `now - creation_time >= ttl`.
* Stale entries trigger a **conditional revalidation** request to the origin using `If‑None‑Match` and `If‑Modified‑Since`.

---

## Installation (docs/installation.md)

### Prerequisites
* **WSL (Ubuntu)** or any Linux environment with `g++` (>= 9) and `make`.
* **Python 3** (for the mock origin server and optional static file server).
* **Node.js** (optional, only if you want to run a development HTTP server for the UI).

### Steps
1. **Clone / create the project** – Already done in `C:\Users\janag\.gemini\antigravity\scratch\HTTP-Proxy-Server-with-Caching`.
2. **Build the backend**:
   ```bash
   cd backend
   make
   ```
   This produces `bin/proxy_server` and `bin/load_balancer`.
3. **Run the mock origin server** (provides test endpoints):
   ```bash
   cd ../scripts
   python3 mock_origin.py 9000   # default port is 9000
   ```
4. **Start the proxy** (listening on 8080 by default):
   ```bash
   cd ../backend
   ./bin/proxy_server -p 8080 -t 8 --policy LRU   # adjust options as needed
   ```
5. **Serve the frontend** (simple static server):
   ```bash
   cd ../../frontend
   python3 -m http.server 8080   # or any other port; it proxies to the same host
   ```
   Open a browser at `http://localhost:8080` – the UI connects to the proxy’s API on the same host/port.

### Configuration
Edit `backend/config/proxy.conf` (or supply a custom file with `-c <path>`). The default file contains all tunable parameters (port, worker threads, cache limits, etc.).

---

## Testing (docs/testing.md)

All unit and integration tests live in `backend/tests/test_runner.cpp`. To run them:
```bash
cd backend
make test   # builds and executes the test binary
```
The test suite verifies:
* HTTP request/response parsing.
* Correct LRU/LFU eviction order.
* Cache‑freshness rules, TTL expiration, and revalidation.
* Bounded queue back‑pressure (HTTP 503 rejection).
* Cache manager revalidation and dynamic policy switching.
* Proper handling of unreachable origin servers (returns 502).

Additionally you can manually exercise the proxy with `curl`:
```bash
# First request – cache miss
curl -i http://localhost:8080/fast
# Second request – should be a cache hit (check X-Proxy-Cache header)
curl -i http://localhost:8080/fast
```
The dashboard UI shows real‑time statistics confirming hit/miss behaviour.

---

## Benchmarking (benchmarks/scripts/bench.sh)

A simple benchmark script using **ApacheBench** (`ab`) is provided. It measures throughput and latency for a given endpoint.
```bash
#!/usr/bin/env bash
# Usage: bench.sh <path> <concurrency> <requests>
PATH=$1
CONC=$2
REQ=$3

URL="http://localhost:8080${PATH}"

echo "Benchmarking $URL with $CONC concurrent connections, $REQ total requests..."
ab -n $REQ -c $CONC $URL
```
Make it executable:
```bash
chmod +x benchmarks/scripts/bench.sh
```
Example:
```bash
./benchmarks/scripts/bench.sh /fast 25 5000
```
The results are saved in `benchmarks/results/` for later analysis.

---

## Usage Summary

| Component | Command | Description |
|-----------|---------|-------------|
| **Mock Origin** | `python3 scripts/mock_origin.py 9000` | Provides `/fast`, `/slow`, `/revalidate`, etc.
| **Proxy Server** | `./backend/bin/proxy_server -p 8080` | Main proxy (configurable via `-c`).
| **Load Balancer** | `./backend/bin/load_balancer -p 8000` | Optional LB distributing to multiple proxy nodes.
| **Frontend UI** | `python3 -m http.server 8080` (in `frontend/`) | Dashboard and mini‑browser.
| **Benchmark** | `benchmarks/scripts/bench.sh /fast 25 5000` | Simple performance test.

---

## Extending the Project
* Enable **cluster mode** by setting `cluster_enabled = true` in `proxy.conf` and configuring `peer_address`/`peer_port`.
* Add new API endpoints under `backend/src/api_handler.cpp`.
* Replace the simple static file server with a proper production web server if needed.
* Integrate more sophisticated load‑balancing algorithms.

---

## License

MIT License – feel free to modify and redistribute.
