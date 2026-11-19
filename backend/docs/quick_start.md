# Quick Start Guide – HTTP Proxy Server with Caching

This guide walks you through setting up, building, and running the full system on a Windows machine with **WSL2 (Ubuntu)**. All commands are shown for the appropriate shell; follow the *Terminal* column.

---

## 1. Prerequisites
| Tool | Why | Install command (WSL) |
|------|-----|------------------------|
| `build-essential` | C++ compiler & make | `sudo apt-get update && sudo apt-get install -y build-essential` |
| `cmake` | Optional build system (not required for current Makefile) | `sudo apt-get install -y cmake` |
| `git` | Source‑control (optional) | `sudo apt-get install -y git` |
| `python3` & `pip` | Mock origin server & optional UI server | `sudo apt-get install -y python3 python3-pip` |
| `node` & `npm` | Front‑end tooling (optional) | `curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash - && sudo apt-get install -y nodejs` |
| `curl` | Command‑line HTTP client | `sudo apt-get install -y curl` |
| `apache2-utils` (for `ab`) | Benchmarking tool | `sudo apt-get install -y apache2-utils` |

> **Tip**: The backend is pure C++17 and builds with `make`; you do not need CMake.

---

## 2. Open the Project in WSL
```powershell
# PowerShell (Windows) – open your WSL home folder
wsl
```
```bash
# Inside WSL – go to the scratch directory
cd /mnt/c/Users/janag/.gemini/antigravity/scratch/HTTP-Proxy-Server-with-Caching
```

---

## 3. Build the Backend
```bash
cd backend
make clean   # optional, removes old objects
make          # builds bin/proxy_server and bin/load_balancer
```
*Artifacts*: `backend/bin/proxy_server` and `backend/bin/load_balancer`.

---

## 4. Start the Mock Origin Server (provides test resources)
```bash
cd ../scripts
# The script runs on port 9000 and serves a handful of endpoints
python3 mock_origin.py
```
Leave this terminal running – it will keep the origin alive.

---

## 5. Launch the Proxy Server
Open a **new** WSL terminal and run:
```bash
cd backend
./bin/proxy_server
```
The proxy listens on **port 8080** by default. You should see a log line similar to:
```
[INFO] Proxy listening on 0.0.0.0:8080
```

---

## 6. Serve the Front‑end UI
The UI is a static site; the easiest way is Python’s built‑in HTTP server.
```bash
cd ../../frontend   # back to the project root then into frontend
python3 -m http.server 8081
```
Open a browser and navigate to **http://localhost:8081**. The dashboard and mini‑browser will automatically call the proxy at `http://localhost:8080`.

---

## 7. Verify Cache Behaviour with `curl`
### First request – should be a MISS
```bash
curl -i http://localhost:8080/fast
```
Look for the header `X-Proxy-Cache: MISS`.

### Second request – should be a HIT
```bash
curl -i http://localhost:8080/fast
```
You should now see `X-Proxy-Cache: HIT`.

---

## 8. Inspect Monitoring API
```bash
curl http://localhost:8080/api/stats | jq
```
The JSON contains:
- `total_requests`
- `cache_hits`
- `cache_misses`
- `active_connections`
- ...
These values drive the charts on the dashboard.

---

## 9. (Optional) Run Two Proxy Instances & Load Balancer
### Start a second proxy on port 8082
```bash
# Terminal 3
cd backend
./bin/proxy_server --port 8082
```
### Start the load balancer (listens on 8080, forwards to 8080 & 8082)
```bash
# Terminal 4
cd backend
./bin/load_balancer
```
The balancer performs health‑checks on each node and returns **503** when the queue is full.

---

## 10. Benchmark the System
```bash
ab -n 1000 -c 50 http://localhost:8080/fast
```
Compare the *Requests per second* and *Time per request* with and without the cache (use `?cache=0` on the mock‑origin endpoint to force a miss).

---

## 11. Graceful Shutdown
- Press **Ctrl‑C** in each terminal (origin, proxy, UI, optional balancer).
- The proxy and balancer will close sockets cleanly and dump final statistics.

---

## 12. Quick‑Reference Cheat‑Sheet
| Action | Terminal | Command |
|--------|----------|---------|
| Build backend | 1 | `cd backend && make` |
| Run mock origin | 2 | `cd scripts && python3 mock_origin.py` |
| Start proxy | 3 | `cd backend && ./bin/proxy_server` |
| Serve UI | 4 | `cd frontend && python3 -m http.server 8081` |
| Test cache | any | `curl -i http://localhost:8080/fast` |
| View stats | any | `curl http://localhost:8080/api/stats | jq` |
| Run balancer | 5 | `cd backend && ./bin/load_balancer` |
| Benchmark | any | `ab -n 1000 -c 50 http://localhost:8080/fast` |

---

**You are now ready to explore the full feature set** – mini‑browser, real‑time charts, LFU vs LRU switch, TTL handling, and back‑pressure behaviour. Enjoy!
