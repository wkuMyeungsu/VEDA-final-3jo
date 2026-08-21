#!/usr/bin/env python3
"""Read-only operations console for the forklift safety services."""
import json
import os
import socket
import subprocess
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
HOST = os.environ.get("SERVER_MONITORING_HOST", "0.0.0.0")
PORT = int(os.environ.get("SERVER_MONITORING_PORT", "8000"))
SYSTEMCTL = os.environ.get("SERVER_MONITORING_SYSTEMCTL", "/bin/systemctl")
MQTT_HOST = os.environ.get("SERVER_MONITORING_MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.environ.get("SERVER_MONITORING_MQTT_PORT", "8883"))
SERVER_LOG = Path(os.environ.get(
    "SERVER_MONITORING_LOG", "/var/log/forklift_safety/storage/server.log"))
RUNTIME_STATUS = Path(os.environ.get(
    "SERVER_MONITORING_STATUS", "/var/log/forklift_safety/runtime/runtime-status.json"))
DEFAULT_REFRESH_INTERVAL_SECONDS = 1
DEFAULT_RECENT_LOG_LINES = 50
MAX_LOG_TAIL_BYTES = 64 * 1024
RUNTIME_STATUS_MAX_AGE_SECONDS = 3


def refresh_interval_seconds():
    """Return the positive UI polling interval configured for this service."""
    raw_value = os.environ.get("SERVER_MONITORING_REFRESH_INTERVAL_SECONDS")
    if raw_value is None:
        return DEFAULT_REFRESH_INTERVAL_SECONDS
    try:
        value = int(raw_value)
    except ValueError:
        return DEFAULT_REFRESH_INTERVAL_SECONDS
    return value if value > 0 else DEFAULT_REFRESH_INTERVAL_SECONDS


def render_index():
    """Render the static console with the service's polling configuration."""
    template = (ROOT / "static" / "index.html").read_text(encoding="utf-8")
    return template.replace(
        "__SERVER_MONITORING_REFRESH_INTERVAL_SECONDS__",
        str(refresh_interval_seconds()),
    )


def service_state(unit):
    """Return a bounded, read-only systemd state without invoking a shell."""
    try:
        result = subprocess.run(
            [SYSTEMCTL, "is-active", unit],
            capture_output=True,
            text=True,
            timeout=2,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    state = result.stdout.strip()
    return state if state else "unknown"


def tcp_reachable(host, port):
    try:
        with socket.create_connection((host, port), timeout=1):
            return True
    except OSError:
        return False


def file_timestamp(path):
    try:
        modified = path.stat().st_mtime
    except OSError:
        return None
    return datetime.fromtimestamp(modified, timezone.utc).isoformat()


def read_runtime_status(path):
    try:
        with path.open(encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def runtime_status_health(snapshot, now=None):
    """Return whether the safety server's structured heartbeat is fresh."""
    if not isinstance(snapshot, dict) or snapshot.get("state") != "online":
        return {"fresh": False, "age_ms": None}
    checked_value = snapshot.get("checked_utc")
    if not checked_value:
        return {"fresh": False, "age_ms": None}
    try:
        checked = datetime.fromisoformat(checked_value.replace("Z", "+00:00"))
    except (TypeError, ValueError):
        return {"fresh": False, "age_ms": None}
    current = now or datetime.now(timezone.utc)
    age_ms = max(0, int((current - checked).total_seconds() * 1000))
    return {
        "fresh": age_ms <= RUNTIME_STATUS_MAX_AGE_SECONDS * 1000,
        "age_ms": age_ms,
    }


def read_recent_logs(path, limit=DEFAULT_RECENT_LOG_LINES):
    """Read a bounded tail of the server log without scanning the whole file."""
    try:
        with path.open("rb") as handle:
            handle.seek(0, os.SEEK_END)
            end_offset = handle.tell()
            start_offset = max(0, end_offset - MAX_LOG_TAIL_BYTES)
            handle.seek(start_offset)
            content = handle.read().decode("utf-8", errors="replace")
    except OSError:
        return []

    lines = content.splitlines()
    if start_offset > 0 and lines:
        lines = lines[1:]
    return lines[-limit:]


def status_snapshot():
    safety_state = service_state("forklift_safety_server.service")
    homography_state = service_state("homography-app.service")
    mqtt_ok = tcp_reachable(MQTT_HOST, MQTT_PORT)
    runtime_status = read_runtime_status(RUNTIME_STATUS)
    runtime_health = runtime_status_health(runtime_status)
    return {
        "ok": safety_state == "active" and mqtt_ok and runtime_health["fresh"],
        "service": "monitoring-app",
        "monitoring": "online",
        "safety_server": {"state": safety_state},
        "homography": {
            "state": homography_state,
            "lifecycle": "on-demand",
        },
        "mqtt": {
            "reachable": mqtt_ok,
            "host": MQTT_HOST,
            "port": MQTT_PORT,
        },
        "server_log": {
            "path": str(SERVER_LOG),
            "modified_utc": file_timestamp(SERVER_LOG),
            "recent_lines": read_recent_logs(SERVER_LOG),
        },
        "runtime_status": {
            "path": str(RUNTIME_STATUS),
            "modified_utc": file_timestamp(RUNTIME_STATUS),
            "fresh": runtime_health["fresh"],
            "age_ms": runtime_health["age_ms"],
            "snapshot": runtime_status,
        },
        "checked_utc": datetime.now(timezone.utc).isoformat(),
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        """성공 polling은 journal에 남기지 않고 HTTP 오류만 남긴다."""
        try:
            status = int(args[1])
        except (IndexError, TypeError, ValueError):
            status = 500
        if status < 400:
            return
        super().log_message(fmt, *args)

    def send_body(self, body, content_type, status=200):
        body = body.encode() if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self.send_body(
                json.dumps(status_snapshot(), ensure_ascii=False),
                "application/json; charset=utf-8",
            )
            return
        if path == "/health/live":
            self.send_body(
                json.dumps({"ok": True, "service": "monitoring-app"}),
                "application/json; charset=utf-8",
            )
            return
        if path in ("/", "/index.html"):
            self.send_body(render_index(), "text/html; charset=utf-8")
            return
        self.send_error(404)


if __name__ == "__main__":
    print(f"server-monitoring listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
