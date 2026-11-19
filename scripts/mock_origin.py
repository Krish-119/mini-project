#!/usr/bin/env python3
"""
Mock Origin Server for HTTP Proxy Server with Caching
Provides controllable HTTP endpoints for cache testing, revalidation, and benchmarking.
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import time
import sys

PORT = 9000

class MockOriginHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Keep logs clean
        sys.stderr.write(f"[MockOrigin] {self.command} {self.path} -> {args[1] if len(args) > 1 else ''}\n")

    def do_GET(self):
        # 1. Fast cacheable endpoint
        if self.path == "/fast" or self.path == "/":
            body = b"{\"message\": \"Hello from Mock Origin!\", \"timestamp\": " + str(time.time()).encode() + b"}"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "public, max-age=30")
            self.send_header("ETag", "\"etag-fast-v1\"")
            self.end_headers()
            self.wfile.write(body)

        # 2. Slow origin endpoint (simulates slow database or remote API)
        elif self.path == "/slow":
            time.sleep(0.2)  # 200ms origin delay
            body = b"{\"message\": \"Slow origin response\", \"delay_ms\": 200, \"time\": " + str(time.time()).encode() + b"}"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "public, max-age=30")
            self.end_headers()
            self.wfile.write(body)

        # 3. Conditional Revalidation endpoint
        elif self.path == "/revalidate":
            etag = self.headers.get("If-None-Match")
            if etag == "\"etag-reval-v1\"":
                # Origin reports content has NOT changed
                self.send_response(304)
                self.send_header("ETag", "\"etag-reval-v1\"")
                self.send_header("Cache-Control", "public, max-age=15")
                self.end_headers()
            else:
                body = b"{\"message\": \"Revalidation origin content\", \"version\": 1.0}"
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "public, max-age=15")
                self.send_header("ETag", "\"etag-reval-v1\"")
                self.end_headers()
                self.wfile.write(body)

        # 4. Short TTL endpoint (expires in 2 seconds)
        elif self.path == "/short-ttl":
            body = b"{\"message\": \"Short TTL response\", \"created_at\": " + str(time.time()).encode() + b"}"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "public, max-age=2")
            self.end_headers()
            self.wfile.write(body)

        # 5. Uncacheable endpoint (no-store)
        elif self.path == "/uncacheable":
            body = b"{\"message\": \"Private uncached data\", \"time\": " + str(time.time()).encode() + b"}"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store, private")
            self.end_headers()
            self.wfile.write(body)

        # 6. Sample HTML page for Mini Browser
        elif self.path == "/html":
            html = """<!DOCTYPE html>
<html>
<head><title>Origin Test Page</title><style>body { font-family: sans-serif; padding: 2rem; color: #2d3748; background: #f7fafc; }</style></head>
<body>
  <h1>Welcome to the Origin Server</h1>
  <p>This page was fetched from mock origin port 9000 through the C++ proxy server.</p>
  <ul>
    <li>Protocol: HTTP/1.1</li>
    <li>Status: 200 OK</li>
    <li>Cache-Control: public, max-age=60</li>
  </ul>
</body>
</html>"""
            body = html.encode('utf-8')
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "public, max-age=60")
            self.end_headers()
            self.wfile.write(body)

        # 7. Large payload
        elif self.path == "/large":
            payload = "A" * (200 * 1024) # 200 KB
            body = payload.encode('utf-8')
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "public, max-age=60")
            self.end_headers()
            self.wfile.write(body)

        else:
            body = b"404 Not Found at Mock Origin"
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    HTTPServer.allow_reuse_address = True
    server = HTTPServer(("0.0.0.0", port), MockOriginHandler)
    print(f"[MockOrigin] Server listening on http://0.0.0.0:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[MockOrigin] Shutting down.")
        server.server_close()
