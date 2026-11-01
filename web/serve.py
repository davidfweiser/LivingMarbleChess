#!/usr/bin/env python3
"""Serves the web build on this computer with the headers the game's threads need.

    python3 web/serve.py [folder] [port]      (default: build-web, port 8080)

then open http://localhost:8080/ (localhost counts as a secure page, so no HTTPS is needed).
"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


folder = sys.argv[1] if len(sys.argv) > 1 else "build-web"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
server = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=folder))
print(f"Living Marble: http://localhost:{port}/  (serving {folder}; Ctrl+C stops)")
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
