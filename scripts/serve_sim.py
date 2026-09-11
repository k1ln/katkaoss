#!/usr/bin/env python3
"""Static file server for the KatKaoss WASM simulator.

Emscripten AUDIO_WORKLET + WASM_WORKERS use SharedArrayBuffer, which requires
the page to be cross-origin isolated. That means every response needs COOP/COEP
headers, which `python3 -m http.server` does not send. This adds them.

Usage: scripts/serve_sim.py <dir> [port]
"""
import http.server
import socketserver
import os
import sys

root = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
os.chdir(root)


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


Handler.extensions_map[".wasm"] = "application/wasm"
Handler.extensions_map[".js"] = "text/javascript"

socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(("", port), Handler) as httpd:
    print(f"Serving {root} at http://localhost:{port}/  (Ctrl-C to stop)")
    httpd.serve_forever()
