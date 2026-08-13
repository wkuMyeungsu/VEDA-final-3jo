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
import threading
import time
import uuid
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, quote, urlparse

# 웹 앱은 CLI 엔진을 직접 구현하지 않고, 허용된 인자만 subprocess로 전달함.
# 결과 파일은 임시 작업 디렉터리에 저장하고 TTL이 지난 뒤 정리함.
ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
CONFIG = ROOT.parent / "config" / "homography_config.json"
CONFIG_VALUE = json.loads(CONFIG.read_text(encoding="utf-8"))
OUTPUTS = CONFIG_VALUE.get("outputs", {})
INPUTS = CONFIG_VALUE.get("inputs", {})
CAMERA_CONFIG = ROOT.parent / "config" / "camera_config.json"
CAMERA = json.loads(CAMERA_CONFIG.read_text(encoding="utf-8")) if CAMERA_CONFIG.is_file() else {}
HOST = os.environ.get("ADMIN_GUI_HOST", "0.0.0.0")
PORT = int(os.environ.get("HOMOGRAPHY_APP_PORT", "8001"))
TOOL = os.environ.get("HOMOGRAPHY_TOOL", "homography_tool")
TIMEOUT = int(os.environ.get("HOMOGRAPHY_COMMAND_TIMEOUT_SEC", "120"))
RESULT_ROOT = Path(os.environ.get("HOMOGRAPHY_RESULT_DIR", "/tmp/homography-results"))
RESULT_TTL_SEC = int(os.environ.get("ADMIN_GUI_RESULT_TTL_SEC", "3600"))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)
OPERATIONAL_HOMOGRAPHY_ROOT = Path(os.environ.get(
    "SAFETY_SERVER_HOMOGRAPHY_DIR",
    str(ROOT.parents[1] / "01_main" / "config" / "homography")))
CAMERA_RETRY_DELAY_SEC = 0.5
LIVE_CAMERA_ROOT = RESULT_ROOT / "live-camera"
LIVE_CAMERA_ROOT.mkdir(parents=True, exist_ok=True)


def configured_output_name(key, fallback):
    """설정된 결과 파일명을 경로 없이 반환함."""
    value = OUTPUTS.get(key, fallback)
    return Path(str(value)).name or fallback


def configured_input_name(key, fallback):
    """설정된 입력 파일명을 경로 없이 반환함."""
    value = INPUTS.get(key, fallback)
    return Path(str(value)).name or fallback


def camera_urls(channel_id=1, profile_override=None):
    """카메라 기본 정보로 RTSP와 스냅샷 URL을 조합함."""
    camera = CAMERA.get("camera", {})
    connection = CAMERA.get("connection", {})
    username = quote(str(camera.get("username", "")), safe="")
    password = quote(str(camera.get("password", "")), safe="")
    ip = str(camera.get("ip", ""))
    rtsp_port = int(camera.get("rtsp_port", 554))
    http_port = int(camera.get("http_port", 80))
    channel_id = int(channel_id)
    if channel_id not in range(1, 5):
        raise ValueError("camera.channel_id must be between 1 and 4")
    profile = str(profile_override or camera.get("profile", "profile2")).strip("/")
    auth = f"{username}:{password}@" if username else ""
    rtsp_url = f"rtsp://{auth}{ip}:{rtsp_port}/{channel_id - 1}/onvif/{profile}/media.smp"
    capture_profile = str(camera.get("capture_profile", "1")).strip("/")
    if capture_profile.lower().startswith("profile"):
        capture_profile = capture_profile[7:]
    capture_profile = quote(capture_profile, safe="")
    snapshot_url = (
        f"http://{ip}:{http_port}/stw-cgi/video.cgi?msubmenu=snapshot&action=view"
        f"&Profile={capture_profile}&Channel={channel_id - 1}"
    )
    return connection, camera, rtsp_url, snapshot_url


def capture_high_resolution_frame(channel_id, timeout):
    """캡처 전용 HTTP 스냅샷에서 원본 JPEG 한 장을 가져옴."""
    connection, camera, _, snapshot_url = camera_urls(channel_id)
    capture_source = str(connection.get("capture_source", "snapshot")).lower()
    if capture_source != "snapshot":
        raise RuntimeError("capture_source must be snapshot")
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, snapshot_url, camera.get("username", ""), camera.get("password", ""))
    opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))
    with opener.open(snapshot_url, timeout=timeout) as response:
        frame = response.read()
    if not frame.startswith(b"\xff\xd8"):
        raise RuntimeError("high-resolution camera snapshot is not JPEG")
    return frame


class CameraStream:
    """채널별 카메라 연결을 유지하고 최신 JPEG 프레임을 보관함."""

    def __init__(self):
        self.condition = threading.Condition()
        self.frames = {}
        self.sequences = {}
        self.active_channel = None
        self.worker = None
        self.stop_event = None
        self.process = None

    def ensure_worker(self, channel_id):
        with self.condition:
            if self.active_channel == channel_id and self.worker and self.worker.is_alive():
                return
            if self.stop_event:
                self.stop_event.set()
            if self.process:
                self.process.kill()
            self.active_channel = channel_id
            self.frames.pop(channel_id, None)
            self.sequences.pop(channel_id, None)
            self.stop_event = threading.Event()
            self.worker = threading.Thread(target=self._receive,
                                           args=(channel_id, self.stop_event), daemon=True)
            self.worker.start()
            self.condition.notify_all()

    def stop(self, channel_id=None):
        with self.condition:
            if channel_id is not None and self.active_channel != channel_id:
                return
            if self.stop_event:
                self.stop_event.set()
            if self.process:
                self.process.kill()
            self.active_channel = None
            self.frames.clear()
            self.sequences.clear()
            self.condition.notify_all()

    def _receive(self, channel_id, stop_event):
        connection, camera, rtsp_url, snapshot_url = camera_urls(channel_id)
        source = str(connection.get("source_type", "rtsp")).lower()
        timeout = float(connection.get("timeout_sec", 10))
        if source == "snapshot":
            self._receive_snapshots(channel_id, camera, snapshot_url, timeout, stop_event)
            return
        command = ["ffmpeg", "-nostdin", "-loglevel", "error", "-rtsp_transport", "tcp",
                   "-i", rtsp_url, "-f", "image2pipe", "-vcodec", "mjpeg", "-q:v", "5", "pipe:1"]
        while not stop_event.is_set():
            process = None
            try:
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                with self.condition:
                    self.process = process
                self._read_mjpeg(channel_id, process, stop_event)
                process.kill()
                process.wait(timeout=2)
            except (OSError, subprocess.SubprocessError, RuntimeError):
                pass
            finally:
                with self.condition:
                    if self.process is process:
                        self.process = None
            if not stop_event.is_set():
                time.sleep(CAMERA_RETRY_DELAY_SEC)

    def _receive_snapshots(self, channel_id, camera, url, timeout, stop_event):
        manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        manager.add_password(None, url, camera.get("username", ""), camera.get("password", ""))
        opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))
        while not stop_event.is_set():
            try:
                with opener.open(url, timeout=timeout) as response:
                    self._set_frame(channel_id, response.read())
            except (OSError, urllib.error.URLError):
                pass
            time.sleep(CAMERA_RETRY_DELAY_SEC)

    def _read_mjpeg(self, channel_id, process, stop_event):
        buffer = b""
        while not stop_event.is_set():
            chunk = process.stdout.read(65536)
            if not chunk:
                return
            buffer += chunk
            while True:
                start = buffer.find(b"\xff\xd8")
                if start < 0:
                    buffer = buffer[-1:]
                    break
                end = buffer.find(b"\xff\xd9", start + 2)
                if end < 0:
                    buffer = buffer[start:]
                    break
                self._set_frame(channel_id, buffer[start:end + 2])
                buffer = buffer[end + 2:]

    def _set_frame(self, channel_id, frame):
        if not frame:
            return
        with self.condition:
            if self.active_channel != channel_id:
                return
            self.frames[channel_id] = frame
            self.sequences[channel_id] = self.sequences.get(channel_id, 0) + 1
            self.condition.notify_all()

    def latest_packet(self, channel_id, timeout=10, previous_sequence=None, stop_event=None, activate=True):
        if stop_event and stop_event.is_set():
            raise RuntimeError("camera channel worker stopped")
        if activate:
            self.ensure_worker(channel_id)
        deadline = time.monotonic() + timeout
        with self.condition:
            while (self.active_channel != channel_id or channel_id not in self.frames or
                   self.sequences[channel_id] == previous_sequence):
                if stop_event and stop_event.is_set():
                    raise RuntimeError("camera channel worker stopped")
                if self.active_channel != channel_id:
                    raise RuntimeError("camera channel switched")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("camera frame unavailable")
                self.condition.wait(remaining)
            return self.sequences[channel_id], self.frames[channel_id]

    def latest(self, channel_id, timeout=10):
        return self.latest_packet(channel_id, timeout)[1]


CAMERA_STREAM = CameraStream()


class CameraDetectionStream:
    """수신 중인 최신 프레임을 C++ 엔진으로 검출하고 오버레이를 갱신함."""

    def __init__(self, source):
        self.source = source
        self.condition = threading.Condition()
        self.frames = {}
        self.sequences = {}
        self.results = {}
        self.active_channel = None
        self.worker = None
        self.stop_event = None

    def ensure_worker(self, channel_id):
        with self.condition:
            if self.active_channel == channel_id and self.worker and self.worker.is_alive():
                return
            if self.stop_event:
                self.stop_event.set()
            self.active_channel = channel_id
            self.frames.pop(channel_id, None)
            self.sequences.pop(channel_id, None)
            self.results.pop(channel_id, None)
            self.stop_event = threading.Event()
            self.worker = threading.Thread(target=self._detect,
                                           args=(channel_id, self.stop_event), daemon=True)
            self.worker.start()
            self.condition.notify_all()

    def stop(self, channel_id=None):
        with self.condition:
            if channel_id is not None and self.active_channel != channel_id:
                return
            if self.stop_event:
                self.stop_event.set()
            self.active_channel = None
            self.frames.clear()
            self.sequences.clear()
            self.results.clear()
            self.condition.notify_all()

    def _detect(self, channel_id, stop_event):
        previous_sequence = None
        input_path = LIVE_CAMERA_ROOT / f"channel-{channel_id}.jpg"
        output_path = LIVE_CAMERA_ROOT / f"channel-{channel_id}-overlay.jpg"
        metadata_path = LIVE_CAMERA_ROOT / f"channel-{channel_id}.json"
        self.source.ensure_worker(channel_id)
        while not stop_event.is_set():
            try:
                previous_sequence, frame = self.source.latest_packet(
                    channel_id, previous_sequence=previous_sequence, stop_event=stop_event,
                    activate=False)
                input_path.write_bytes(frame)
                result = run_tool(["detect-markers", "--config", str(CONFIG),
                                   "--input", str(input_path), "--output", str(metadata_path),
                                   "--overlay", str(output_path)])
                if result["ok"] and output_path.is_file():
                    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
                    self._set_result(channel_id, metadata, output_path.read_bytes())
                elif not result["ok"]:
                    time.sleep(CAMERA_RETRY_DELAY_SEC)
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
                time.sleep(CAMERA_RETRY_DELAY_SEC)

    def _set_result(self, channel_id, metadata, frame):
        with self.condition:
            if self.active_channel != channel_id:
                return
            self.results[channel_id] = metadata
            self.frames[channel_id] = frame
            self.sequences[channel_id] = self.sequences.get(channel_id, 0) + 1
            self.condition.notify_all()

    def current_result(self, channel_id):
        self.ensure_worker(channel_id)
        with self.condition:
            return self.results.get(channel_id, {"ids": [], "corners": [], "image_size": {}})

    def latest_packet(self, channel_id, timeout=10, previous_sequence=None, activate=True):
        if activate:
            self.ensure_worker(channel_id)
        deadline = time.monotonic() + timeout
        with self.condition:
            while (self.active_channel != channel_id or channel_id not in self.frames or
                   self.sequences[channel_id] == previous_sequence):
                if self.active_channel != channel_id:
                    raise RuntimeError("detected camera channel switched")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("detected camera frame unavailable")
                self.condition.wait(remaining)
            return self.sequences[channel_id], self.frames[channel_id]

    def latest(self, channel_id, timeout=10):
        return self.latest_packet(channel_id, timeout)[1]


CAMERA_DETECTION_STREAM = CameraDetectionStream(CAMERA_STREAM)


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


def capture_directory(capture_id):
    """외부 입력 capture_id를 결과 루트 바로 아래의 안전한 디렉터리로 제한함."""
    if not isinstance(capture_id, str) or len(capture_id) != 32 or any(
            character not in "0123456789abcdef" for character in capture_id):
        raise ValueError("invalid capture_id")
    directory = RESULT_ROOT / capture_id
    if not directory.is_dir() or not (directory / "capture.jpg").is_file():
        raise ValueError("capture_id not found or expired")
    return directory


def multiply_homographies(left, right):
    """중간 라이브러리 없이 3×3 호모그래피를 합성하고 정규화함."""
    matrix = [[sum(float(left[row][k]) * float(right[k][column]) for k in range(3))
               for column in range(3)] for row in range(3)]
    scale = matrix[2][2]
    if abs(scale) < 1e-12:
        raise ValueError("invalid composed homography")
    return [[value / scale for value in row] for row in matrix]


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
        if path == "/api/camera/frame":
            try:
                query = urlparse(self.path).query
                channel_id = int(parse_qs(query).get("channel", [1])[0])
                body = CAMERA_STREAM.latest(channel_id)
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (OSError, urllib.error.URLError, subprocess.SubprocessError, RuntimeError, ValueError) as error:
                self.send_json({"ok": False, "error": str(error)}, 502)
            return
        if path == "/api/camera/video":
            stream_started = False
            try:
                query = urlparse(self.path).query
                params = parse_qs(query)
                channel_id = int(params.get("channel", [1])[0])
                stream = CAMERA_DETECTION_STREAM if params.get("overlay", ["0"])[0] == "1" else CAMERA_STREAM
                camera_urls(channel_id)
                stream.ensure_worker(channel_id)
                sequence, frame = stream.latest_packet(channel_id, activate=False)
                self.send_response(200)
                self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "close")
                self.end_headers()
                stream_started = True
                while True:
                    self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                     str(len(frame)).encode() + b"\r\n\r\n" + frame + b"\r\n")
                    self.wfile.flush()
                    sequence, frame = stream.latest_packet(channel_id, previous_sequence=sequence,
                                                            activate=False)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except (OSError, ValueError, RuntimeError) as error:
                if not stream_started and not self.wfile.closed:
                    self.send_json({"ok": False, "error": str(error)}, 502)
            return
        if path == "/api/camera/detections":
            try:
                query = urlparse(self.path).query
                channel_id = int(parse_qs(query).get("channel", [1])[0])
                camera_urls(channel_id)
                self.send_json(CAMERA_DETECTION_STREAM.current_result(channel_id))
            except (OSError, RuntimeError, ValueError) as error:
                self.send_json({"ok": False, "error": str(error)}, 502)
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

        if path == "/api/camera/detect":
            cleanup_results()
            job_id = uuid.uuid4().hex
            job_dir = RESULT_ROOT / job_id
            job_dir.mkdir(parents=True)
            try:
                channel_id = int(payload.get("channel", 1))
                image_path = job_dir / "capture.jpg"
                CAMERA_STREAM.ensure_worker(channel_id)
                preview_connection, _, _, _ = camera_urls(channel_id)
                capture_timeout = float(preview_connection.get("timeout_sec", 10))
                _, preview_frame = CAMERA_STREAM.latest_packet(channel_id, timeout=capture_timeout,
                                                                activate=False)
                preview_path = job_dir / "rtsp-capture.jpg"
                preview_path.write_bytes(preview_frame)
                frame = capture_high_resolution_frame(channel_id, capture_timeout)
                image_path.write_bytes(frame)
                CAMERA_DETECTION_STREAM.stop(channel_id)
                CAMERA_STREAM.stop(channel_id)
                output_path = job_dir / "markers.json"
                overlay_path = job_dir / "markers-overlay.png"
                result = run_tool(["detect-markers", "--config", str(CONFIG),
                                   "--input", str(image_path), "--output", str(output_path),
                                   "--overlay", str(overlay_path)])
                if not result["ok"]:
                    raise RuntimeError(result["stderr"] or "marker detection failed")
                detected = json.loads(output_path.read_text(encoding="utf-8"))
                rtsp_output_path = job_dir / "rtsp-markers.json"
                rtsp_result = run_tool(["detect-markers", "--config", str(CONFIG),
                                        "--input", str(preview_path), "--output", str(rtsp_output_path)])
                if not rtsp_result["ok"]:
                    raise RuntimeError(rtsp_result["stderr"] or "RTSP marker detection failed")
                rtsp_detected = json.loads(rtsp_output_path.read_text(encoding="utf-8"))
                alignment_path = job_dir / "rtsp-alignment.json"
                alignment_result = run_tool(["align-markers", "--config", str(CONFIG),
                    "--source", str(preview_path), "--destination", str(image_path),
                    "--output", str(alignment_path)])
                alignment = None
                alignment_error = ""
                if alignment_result["ok"]:
                    alignment = json.loads(alignment_path.read_text(encoding="utf-8"))
                else:
                    alignment_error = alignment_result["stderr"].strip() or "공통 마커가 부족합니다."
                detected["overlay_url"] = f"/artifacts/{job_id}/markers-overlay.png"
                detected["image_url"] = f"/artifacts/{job_id}/capture.jpg"
                detected["capture_id"] = job_id
                detected["channel"] = channel_id
                detected["rtsp_detection"] = rtsp_detected
                detected["rtsp_image_url"] = f"/artifacts/{job_id}/rtsp-capture.jpg"
                detected["rtsp_alignment"] = alignment
                detected["rtsp_alignment_error"] = alignment_error
                (job_dir / "capture-meta.json").write_text(json.dumps({
                    "capture_id": job_id, "channel": channel_id,
                    "capture_image_size": detected.get("image_size", {}),
                    "rtsp_image_size": rtsp_detected.get("image_size", {})
                }, ensure_ascii=False, indent=2), encoding="utf-8")
                self.send_json({"ok": True, "result": detected})
            except (OSError, urllib.error.URLError, subprocess.SubprocessError, RuntimeError,
                    ValueError, TypeError, json.JSONDecodeError) as error:
                shutil.rmtree(job_dir, ignore_errors=True)
                self.send_json({"ok": False, "error": str(error)}, 502)
            return
        if path == "/api/homography/solve":
            try:
                capture_id = payload.get("capture_id")
                job_dir = capture_directory(capture_id)
                marker_size_mm = float(payload.get("marker_size_mm"))
                reference_marker_id = int(payload.get("reference_marker_id"))
                excluded_ids = sorted(set(int(value) for value in payload.get("excluded_ids", [])))
                if marker_size_mm <= 0 or marker_size_mm > 100000:
                    raise ValueError("marker_size_mm must be positive")
                layout = {"marker_size_mm": marker_size_mm,
                          "reference_marker_id": reference_marker_id,
                          "excluded_ids": excluded_ids,
                          "axis_origin_px": payload.get("axis_origin_px"),
                          "axis_x_end_px": payload.get("axis_x_end_px"),
                          "axis_y_end_px": payload.get("axis_y_end_px"),
                          "axis_x_length_mm": payload.get("axis_x_length_mm"),
                          "axis_y_length_mm": payload.get("axis_y_length_mm"),
                          "measurements": payload.get("measurements", [])}
                layout_file = job_dir / "layout.json"
                layout_file.write_text(json.dumps(layout, ensure_ascii=False), encoding="utf-8")
                output_name = configured_output_name("manual", "homography_manual.json")
                if Path(output_name).suffix.lower() != ".json":
                    output_name += ".json"
                output_file = job_dir / output_name
                overlay_name = "homography-overlay.png"
                result = run_tool(["solve-manual", "--config", str(CONFIG),
                    "--input", str(job_dir / "capture.jpg"), "--layout", str(layout_file),
                    "--output", str(output_file), "--overlay", str(job_dir / overlay_name)])
                if not result["ok"]:
                    self.send_json(result, 422)
                    return
                value = json.loads(output_file.read_text(encoding="utf-8"))
                alignment_file = job_dir / "rtsp-alignment.json"
                if alignment_file.is_file():
                    alignment = json.loads(alignment_file.read_text(encoding="utf-8"))
                    value["H_rtsp_pixel_to_capture"] = alignment["H_source_to_destination"]
                    value["H_rtsp_pixel_to_world"] = multiply_homographies(
                        value["H_capture_pixel_to_world"], alignment["H_source_to_destination"])
                    value["rtsp_image_size"] = alignment["source_size"]
                    value["rtsp_alignment_rmse_px"] = alignment["rmse_px"]
                    value["rtsp_common_ids"] = alignment["common_ids"]
                else:
                    value["H_rtsp_pixel_to_world"] = None
                    value["rtsp_alignment_error"] = "캡처 시 공통 마커가 부족해 RTSP 정합을 만들지 못했습니다."
                value["capture_id"] = capture_id
                value["channel"] = json.loads((job_dir / "capture-meta.json").read_text(
                    encoding="utf-8"))["channel"] if (job_dir / "capture-meta.json").is_file() else None
                value.setdefault("verification_region_world", None)
                output_file.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                                       encoding="utf-8")
                channel = int(value["channel"])
                if channel < 1:
                    raise ValueError("보정 채널은 1 이상이어야 합니다")
                if value.get("world_unit") != "mm":
                    raise ValueError("호모그래피 world_unit은 mm여야 합니다")
                OPERATIONAL_HOMOGRAPHY_ROOT.mkdir(parents=True, exist_ok=True)
                operational_file = OPERATIONAL_HOMOGRAPHY_ROOT / f"homography_channel_{channel}_mm.json"
                temporary_file = operational_file.with_suffix(".json.tmp")
                shutil.copyfile(output_file, temporary_file)
                temporary_file.replace(operational_file)
                self.send_json({"ok": True, "result": value,
                    "artifact_url": f"/artifacts/{capture_id}/{output_name}",
                    "overlay_url": f"/artifacts/{capture_id}/{overlay_name}",
                    "operational_path": str(operational_file)})
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self.send_json({"ok": False, "error": str(error)}, 400)
            return
        if path == "/api/homography/region":
            try:
                job_dir = capture_directory(payload.get("capture_id"))
                region = payload.get("verification_region_world")
                if not isinstance(region, list) or len(region) != 4 or any(
                        not isinstance(point, list) or len(point) != 2 for point in region):
                    raise ValueError("verification_region_world must contain four [x,y] points")
                region = [[float(value) for value in point] for point in region]
                output_name = configured_output_name("manual", "homography_manual.json")
                if Path(output_name).suffix.lower() != ".json": output_name += ".json"
                output_file = job_dir / output_name
                value = json.loads(output_file.read_text(encoding="utf-8"))
                value["verification_region_world"] = region
                output_file.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                                       encoding="utf-8")
                self.send_json({"ok": True, "result": value})
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self.send_json({"ok": False, "error": str(error)}, 400)
            return
        if path == "/api/homography/calibrate":
            args = ["calibrate", "--config", str(CONFIG), "--input",
                    configured_input_name("image", "capture.png"),
                    "--output", configured_output_name("calibration", "homography.json")]
            for key, flag in (("channel", "--channel"), ("max_rmse_mm", "--max-rmse-mm")):
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
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    print(f"homography-app listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
