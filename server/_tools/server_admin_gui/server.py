#!/usr/bin/env python3
"""Small LAN-only web shell for server tools.

The dashboard is deliberately read-only for the server itself at this stage.
Homography actions are allow-listed subprocess calls; no shell is used.
"""
import json
import mimetypes
import os
import subprocess
import shutil
import tempfile
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
HOST = os.environ.get("ADMIN_GUI_HOST", "0.0.0.0")
PORT = int(os.environ.get("ADMIN_GUI_PORT", "8000"))
TOOL = os.environ.get("HOMOGRAPHY_TOOL", "homography_tool")
TIMEOUT = int(os.environ.get("HOMOGRAPHY_COMMAND_TIMEOUT_SEC", "120"))
RESULT_ROOT = Path(os.environ.get("ADMIN_GUI_RESULT_DIR", "/tmp/server-admin-gui-results"))
RESULT_TTL_SEC = int(os.environ.get("ADMIN_GUI_RESULT_TTL_SEC", "3600"))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)


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


def cleanup_results():
    now = time.time()
    for path in RESULT_ROOT.iterdir():
        try:
            if now - path.stat().st_mtime > RESULT_TTL_SEC:
                shutil.rmtree(path) if path.is_dir() else path.unlink()
        except FileNotFoundError:
            pass


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
        if path.startswith("/artifacts/"):
            parts = path.split("/")
            if len(parts) != 4 or not parts[2] or not parts[3]:
                self.send_error(404)
                return
            candidate = (RESULT_ROOT / parts[2] / parts[3]).resolve()
            if os.path.commonpath((str(RESULT_ROOT.resolve()), str(candidate))) != str(RESULT_ROOT.resolve()) or not candidate.is_file():
                self.send_error(404)
                return
            self.serve_file(candidate)
            return
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
            output_name = Path(payload["output"]).name
            if Path(output_name).suffix.lower() not in (".svg", ".png"):
                self.send_json({"ok": False, "error": "output must end with .svg or .png"}, 400)
                return
            cleanup_results()
            job_id = uuid.uuid4().hex
            job_dir = RESULT_ROOT / job_id
            job_dir.mkdir()
            output_path = job_dir / output_name
            args = ["gen-board", "--config", payload["config"], "--output", str(output_path)]
            options = (("board_width_mm", "--board-width-mm"),
                       ("board_height_mm", "--board-height-mm"),
                       ("margin_mm", "--margin-mm"), ("dpi", "--dpi"))
            for key, flag in options:
                if payload.get(key) not in (None, ""):
                    args.extend((flag, str(payload[key])))
            for key, flag in (("no_ids", "--no-ids"), ("no_origin", "--no-origin"),
                              ("no_grid", "--no-grid")):
                if payload.get(key): args.append(flag)
            result = run_tool(args)
            if result["ok"]:
                result["artifact_url"] = f"/artifacts/{job_id}/{output_name}"
                result["preview_url"] = result["artifact_url"]
                result["note"] = "결과는 임시 파일이며 자동 만료됩니다. 브라우저에서 다운로드하세요."
            else:
                shutil.rmtree(job_dir, ignore_errors=True)
            self.send_json(result)
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
