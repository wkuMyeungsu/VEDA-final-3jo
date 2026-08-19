#!/usr/bin/env python3
"""Minimal server operations console placeholder.

This service intentionally does not invoke Homography or control the safety
server yet. It only provides the future operations-console landing page.
"""
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
HOST = os.environ.get("SERVER_MONITORING_HOST", "0.0.0.0")
PORT = int(os.environ.get("SERVER_MONITORING_PORT", "8000"))


class Handler(BaseHTTPRequestHandler):
    def send_body(self, body, content_type):
        body = body.encode() if isinstance(body, str) else body
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self.send_body(json.dumps({"ok": True, "service": "server-monitoring", "monitoring": "placeholder", "port": PORT}), "application/json")
            return
        if path in ("/", "/index.html"):
            self.send_body((ROOT / "static" / "index.html").read_bytes(), "text/html; charset=utf-8")
            return
        self.send_error(404)


if __name__ == "__main__":
    print(f"server-monitoring listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
