#!/usr/bin/env python3
"""서버 도구를 호출하는 LAN용 웹 셸.

호모그래피 명령은 허용된 인자만 셸 없이 subprocess로 실행함.
"""
import json
import base64
import mimetypes
import os
import subprocess
import shutil
import tempfile
import time
import uuid
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

# 웹 앱은 CLI 엔진을 직접 구현하지 않고, 허용된 인자만 subprocess로 전달함.
# 결과 파일은 임시 작업 디렉터리에 저장하고 TTL이 지난 뒤 정리함.
ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
CONFIG = ROOT.parent / "config" / "homography_config.json"
CONFIG_VALUE = json.loads(CONFIG.read_text(encoding="utf-8"))
OUTPUTS = CONFIG_VALUE.get("outputs", {})
INPUTS = CONFIG_VALUE.get("inputs", {})
HOST = os.environ.get("ADMIN_GUI_HOST", "0.0.0.0")
PORT = int(os.environ.get("HOMOGRAPHY_APP_PORT", "8001"))
TOOL = os.environ.get("HOMOGRAPHY_TOOL", "homography_tool")
TIMEOUT = int(os.environ.get("HOMOGRAPHY_COMMAND_TIMEOUT_SEC", "120"))
RESULT_ROOT = Path(os.environ.get("HOMOGRAPHY_RESULT_DIR", "/tmp/homography-results"))
RESULT_TTL_SEC = int(os.environ.get("ADMIN_GUI_RESULT_TTL_SEC", "3600"))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)


def configured_output_name(key, fallback):
    """설정된 결과 파일명을 경로 없이 반환함."""
    value = OUTPUTS.get(key, fallback)
    return Path(str(value)).name or fallback


def configured_input_name(key, fallback):
    """설정된 입력 파일명을 경로 없이 반환함."""
    value = INPUTS.get(key, fallback)
    return Path(str(value)).name or fallback


def run_tool(args):
    """실행 파일을 셸 없이 호출하고 웹 API용 결과 객체로 변환함."""
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
    """보관 시간이 지난 결과 파일과 작업 디렉터리 삭제함."""
    now = time.time()
    for path in RESULT_ROOT.iterdir():
        try:
            if now - path.stat().st_mtime > RESULT_TTL_SEC:
                shutil.rmtree(path) if path.is_dir() else path.unlink()
        except FileNotFoundError:
            pass


class Handler(BaseHTTPRequestHandler):
    """정적 파일, 상태 확인, 호모그래피 CLI 호출을 제공하는 HTTP 핸들러."""

    def log_message(self, fmt, *args):
        print(f"[admin-gui] {self.address_string()} - {fmt % args}")

    def send_json(self, value, status=200):
        """JSON 응답의 헤더와 본문을 일관된 형식으로 전송함."""
        body = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        """정적 리소스·임시 산출물·상태 API 처리함."""
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
        if path.startswith("/static/"):
            relative = Path(path.removeprefix("/static/"))
            candidate = (STATIC / relative).resolve()
            if os.path.commonpath((str(STATIC.resolve()), str(candidate))) != str(STATIC.resolve()):
                self.send_error(404)
                return
            self.serve_file(candidate)
            return
        self.send_error(404)

    def do_POST(self):
        """JSON 요청 검증 및 허용된 호모그래피 명령 실행함."""
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0"))
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except (ValueError, UnicodeDecodeError):
            self.send_json({"ok": False, "error": "invalid JSON"}, 400)
            return

        if path == "/api/homography/gen-markers":
            config = str(CONFIG)
            try:
                last_id = int(payload.get("last_id", 15))
                size_mm = float(payload.get("size_mm", 100))
                dpi = float(payload.get("dpi", 300))
                margin_mm = float(payload.get("margin_mm", 20))
            except (TypeError, ValueError):
                self.send_json({"ok": False, "error": "last_id, size_mm, margin_mm and dpi must be numbers"}, 400)
                return
            fmt = str(payload.get("format", "png")).lower()
            if last_id < 0 or last_id > 999 or size_mm <= 0 or dpi <= 0 or fmt not in ("png", "svg"):
                self.send_json({"ok": False, "error": "invalid marker generation parameters"}, 400)
                return
            cleanup_results()
            job_id = uuid.uuid4().hex
            job_dir = RESULT_ROOT / job_id
            job_dir.mkdir(parents=True)
            generated = []
            if margin_mm < 0:
                self.send_json({"ok": False, "error": "margin_mm must be non-negative"}, 400)
                return
            for marker_id in range(last_id + 1):
                name = f"aruco_id_{marker_id:03d}.{fmt}"
                label = f"{marker_id:02d}"
                result = run_tool(["gen-marker", "--config", config, "--id", str(marker_id),
                                   "--output", str(job_dir / name), "--size-mm", str(size_mm),
                                   "--margin-mm", str(margin_mm), "--label", label,
                                   "--dpi", str(dpi)])
                if not result["ok"]:
                    shutil.rmtree(job_dir, ignore_errors=True)
                    self.send_json(result, 500)
                    return
                generated.append(name)
            zip_name = configured_output_name("markers_zip", "aruco_markers.zip")
            if Path(zip_name).suffix.lower() != ".zip":
                zip_name += ".zip"
            zip_path = job_dir / zip_name
            with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for name in generated:
                    archive.write(job_dir / name, arcname=name)
                manifest = {
                    "config": config, "first_id": 0, "last_id": last_id,
                    "count": len(generated), "size_mm": size_mm, "margin_mm": margin_mm, "dpi": dpi,
                    "format": fmt
                }
                archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
            preview_name = "preview.html"
            preview = ["<!doctype html><meta charset='utf-8'><title>ArUco marker preview</title>",
                       "<style>body{font-family:system-ui;margin:24px} .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:16px} figure{margin:0;text-align:center} img{max-width:100%;border:1px solid #bbb} figcaption{margin-top:6px}</style>",
                       "<h1>ArUco markers</h1>",
                       f"<p>마커 {size_mm:g} mm / 여백 {margin_mm:g} mm / 총 {len(generated)}개</p>", "<div class='grid'>"]
            for name in generated:
                preview.append(f"<figure><img src='{name}'><figcaption>{name}</figcaption></figure>")
            preview.append("</div>")
            (job_dir / preview_name).write_text("\n".join(preview), encoding="utf-8")
            result = {"ok": True, "stdout": f"generated {len(generated)} markers\n", "stderr": "",
                      "returncode": 0, "count": len(generated),
                      "artifact_url": f"/artifacts/{job_id}/{zip_name}",
                      "preview_url": f"/artifacts/{job_id}/{preview_name}",
                      "note": "ZIP 안에 ID별 이미지와 manifest.json이 들어 있습니다. 결과는 임시 파일이며 자동 만료됩니다."}
            self.send_json(result)
            return
        if path == "/api/homography/calibrate":
            args = ["calibrate", "--config", str(CONFIG), "--input",
                    configured_input_name("image", "capture.png"),
                    "--output", configured_output_name("calibration", "homography.json")]
            for key, flag in (("channel", "--channel"), ("max_rmse_cm", "--max-rmse-cm")):
                if payload.get(key) not in (None, ""):
                    args.extend((flag, str(payload[key])))
            self.send_json(run_tool(args))
            return
        if path == "/api/homography/solve-manual":
            layout = payload.get("layout")
            if not isinstance(layout, dict):
                self.send_json({"ok": False, "error": "layout is required"}, 400)
                return
            image_data = payload.get("image_data", "")
            input_path = payload.get("input", "")
            cleanup_results()
            job_id = uuid.uuid4().hex
            job_dir = RESULT_ROOT / job_id
            job_dir.mkdir(parents=True)
            if isinstance(image_data, str) and image_data.startswith("data:") and "," in image_data:
                try:
                    image_bytes = base64.b64decode(image_data.split(",", 1)[1], validate=True)
                except (ValueError, base64.binascii.Error):
                    shutil.rmtree(job_dir, ignore_errors=True)
                    self.send_json({"ok": False, "error": "invalid image_data"}, 400)
                    return
                input_file = job_dir / "input-image"
                input_file.write_bytes(image_bytes)
            elif isinstance(input_path, str) and input_path:
                input_file = Path(input_path)
                if not input_file.is_file():
                    shutil.rmtree(job_dir, ignore_errors=True)
                    self.send_json({"ok": False, "error": "input image not found"}, 400)
                    return
            else:
                shutil.rmtree(job_dir, ignore_errors=True)
                self.send_json({"ok": False, "error": "image_data or input is required"}, 400)
                return
            layout_file = job_dir / "layout.json"
            layout_file.write_text(json.dumps(layout, ensure_ascii=False), encoding="utf-8")
            manual_name = configured_output_name("manual", "homography_manual.json")
            if Path(manual_name).suffix.lower() != ".json": manual_name += ".json"
            overlay_name = "homography-overlay.png"
            result = run_tool(["solve-manual", "--config", str(CONFIG), "--input", str(input_file),
                               "--layout", str(layout_file), "--output", str(job_dir / manual_name),
                               "--overlay", str(job_dir / overlay_name)])
            if result["ok"]:
                result["artifact_url"] = f"/artifacts/{job_id}/{manual_name}"
                result["overlay_url"] = f"/artifacts/{job_id}/{overlay_name}"
                result["result"] = json.loads((job_dir / manual_name).read_text(encoding="utf-8"))
                result["note"] = "호모그래피 JSON과 검증용 오버레이를 생성했습니다. 결과는 임시 파일입니다."
            else:
                shutil.rmtree(job_dir, ignore_errors=True)
            self.send_json(result, 200 if result["ok"] else 500)
            return
        if path == "/api/homography/view":
            args = ["view", "--config", str(CONFIG), "--homography",
                    configured_output_name("calibration", "homography.json"),
                    "--input", configured_input_name("image", "capture.png"), "--output-dir",
                    configured_output_name("view_dir", "view_result")]
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
    print(f"homography-app listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
