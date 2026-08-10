#!/usr/bin/env python3
"""Small LAN-only web shell for server tools.

The dashboard is deliberately read-only for the server itself at this stage.
Homography actions are allow-listed subprocess calls; no shell is used.
"""
import json
import mimetypes
import os
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
HOST = os.environ.get("ADMIN_GUI_HOST", "0.0.0.0")
PORT = int(os.environ.get("ADMIN_GUI_PORT", "8000"))
TOOL = os.environ.get("HOMOGRAPHY_TOOL", "homography_tool")
TIMEOUT = int(os.environ.get("HOMOGRAPHY_COMMAND_TIMEOUT_SEC", "120"))


def run_tool(args):
    try:
        result = subprocess.run([TOOL, *args], capture_output=True, text=True,
                                timeout=TIMEOUT, check=False)
        return {"ok": result.returncode == 0, "returncode": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    except FileNotFoundError:
        return {"ok": False, "returncode": 127, "stdout": "",
                "stderr": f"homography tool not found: {TOOL}"}
    except subprocess.TimeoutExpired:
        return {"ok": False, "returncode": 124, "stdout": "",
                "stderr": f"command timed out after {TIMEOUT}s"}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[admin-gui] {self.address_string()} - {fmt % args}")

    def send_json(self, value, status=200):
        body = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self.send_json({"ok": True, "server_monitoring": "placeholder",
                            "homography_tool": TOOL, "port": PORT})
            return
        if path == "/" or path == "/index.html":
            self.serve_file(STATIC / "index.html", "text/html; charset=utf-8")
            return
        self.send_error(404)

    def do_POST(self):
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0"))
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except (ValueError, UnicodeDecodeError):
            self.send_json({"ok": False, "error": "invalid JSON"}, 400)
            return

        if path == "/api/homography/selftest":
            self.send_json(run_tool(["selftest", "--verbose"]))
            return
        if path == "/api/homography/gen-board":
            required = ("config", "output")
            if any(not isinstance(payload.get(key), str) or not payload[key] for key in required):
                self.send_json({"ok": False, "error": "config and output are required"}, 400)
                return
            args = ["gen-board", "--config", payload["config"], "--output", payload["output"]]
            options = (("board_width_mm", "--board-width-mm"),
                       ("board_height_mm", "--board-height-mm"),
                       ("margin_mm", "--margin-mm"), ("dpi", "--dpi"))
            for key, flag in options:
                if payload.get(key) not in (None, ""):
                    args.extend((flag, str(payload[key])))
            for key, flag in (("no_ids", "--no-ids"), ("no_origin", "--no-origin"),
                              ("no_grid", "--no-grid")):
                if payload.get(key): args.append(flag)
            self.send_json(run_tool(args))
            return
        if path == "/api/homography/calibrate":
            required = ("config", "input", "output")
            if any(not isinstance(payload.get(key), str) or not payload[key] for key in required):
                self.send_json({"ok": False, "error": "config, input and output are required"}, 400)
                return
            args = ["calibrate", "--config", payload["config"], "--input", payload["input"],
                    "--output", payload["output"]]
            for key, flag in (("channel", "--channel"), ("max_rmse_cm", "--max-rmse-cm")):
                if payload.get(key) not in (None, ""):
                    args.extend((flag, str(payload[key])))
            self.send_json(run_tool(args))
            return
        if path == "/api/homography/view":
            required = ("config", "homography", "input", "output_dir")
            if any(not isinstance(payload.get(key), str) or not payload[key] for key in required):
                self.send_json({"ok": False, "error": "config, homography, input and output_dir are required"}, 400)
                return
            args = ["view", "--config", payload["config"], "--homography", payload["homography"],
                    "--input", payload["input"], "--output-dir", payload["output_dir"]]
            self.send_json(run_tool(args))
            return
        self.send_json({"ok": False, "error": "unknown endpoint"}, 404)

    def serve_file(self, path, content_type=None):
        try:
            body = path.read_bytes()
        except FileNotFoundError:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type or mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    print(f"server-admin-gui listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
