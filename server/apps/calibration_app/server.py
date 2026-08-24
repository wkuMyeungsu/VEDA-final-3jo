#!/usr/bin/env python3
"""카메라 내부 파라미터 캘리브레이션 앱 (포트 8002).

호모그래피 앱(:8001)의 미리보기 경로로 CCTV 원본 프레임을 받아 ChArUco 보드로
카메라 캘리브레이션을 수행하고, 산출물(K, 왜곡계수 JSON)을 저장한다.

검출·수치는 homography_tool calibrate-intrinsics(C++, OpenCV)가 전담하며
이 서버는 수집·실행·표시만 한다.
"""

import json
import os
import shutil
import time
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

APP_ROOT = Path(__file__).resolve().parents[1] / "homography_app"
CONFIG = Path(os.environ.get(
    "HOMOGRAPHY_CONFIG_DIR",
    str(Path(__file__).resolve().parents[2] / "config" / "homography"))) \
    / "homography_config.json"
TOOL = Path(os.environ.get(
    "HOMOGRAPHY_TOOL", str(APP_ROOT / "processing" / "build" / "homography_tool")))
HOMOGRAPHY_BASE = os.environ.get("HOMOGRAPHY_APP_URL", "http://127.0.0.1:8001")
HOST = os.environ.get("CALIBRATION_APP_HOST", "0.0.0.0")
PORT = int(os.environ.get("CALIBRATION_APP_PORT", "8002"))
FRAME_TIMEOUT_SEC = 20
GRAB_INTERVAL_SEC = 1.2   # 성공한 캡처 사이 대기 - 보드 자세를 바꿀 시간

RESULT_ROOT = Path(os.environ.get("CALIBRATION_RESULT_DIR", "/tmp/calibration-results"))
# 산출물 보관 위치. 호모그래피 앱의 운영 H와 같은 루트를 공유한다.
OUTPUT_PATH = Path(os.environ.get(
    "CAMERA_INTRINSICS_PATH",
    str(Path(os.environ.get("SAFETY_SERVER_HOMOGRAPHY_DIR",
                            "/etc/forklift_safety/homography")) / "camera_intrinsics.json")))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)
OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)

PAGE = """<!doctype html>
<html lang="ko"><meta charset="utf-8">
<title>카메라 캘리브레이션</title>
<style>
 body{font-family:system-ui,sans-serif;max-width:720px;margin:40px auto;padding:0 16px}
 button{font-size:1rem;padding:10px 24px}
 pre{background:#0b1724;color:#d7e5f2;padding:12px;overflow:auto;white-space:pre-wrap}
 .hint{color:#556}
</style>
<h1>카메라 캘리브레이션</h1>
<p class="hint">ChArUco 보드(DICT_4X4_50 · 7×5 · 정사각 38mm · 마커 19mm)를 카메라 앞에서
여러 각도로 기울인 채 들고 있으면 프레임을 수집한다. 최소 6장, 권장 12장 이상.</p>
<button id="run">캘리브레이션 시작</button> <span id="status">대기 중</span>
<pre id="result">결과가 여기 표시됩니다.</pre>
<script>
const $ = (sel) => document.querySelector(sel);
$('#run').onclick = async () => {
  $('#run').disabled = true;
  $('#status').textContent = '프레임 수집·산출 중… (보드를 천천히 움직이세요)';
  try {
    const response = await fetch('/api/calibrate', {method: 'POST'});
    const value = await response.json();
    if (!value.ok) throw new Error(value.error || 'HTTP ' + response.status);
    const r = value.result;
    $('#status').textContent =
      `완료 · ${r.frames_used}장 · 재투영 RMSE ${Number(r.reprojection_rmse_px).toFixed(3)} px`;
    $('#result').textContent = JSON.stringify(r, null, 2);
  } catch (error) {
    $('#status').textContent = '실패';
    $('#result').textContent = String(error);
  } finally {
    $('#run').disabled = false;
  }
};
</script>
"""


def grab_frame(job_dir, index):
    """호모그래피 앱 미리보기 경로에서 원본 프레임 한 장을 받는다."""
    # snapshot=1: RTSP 미리보기(800x600) 대신 HTTP 스냅샷(profile1)의 원본 2592x1520 JPEG.
    request = urllib.request.Request(f"{HOMOGRAPHY_BASE}/api/camera/frame?snapshot=1")
    with urllib.request.urlopen(request, timeout=FRAME_TIMEOUT_SEC) as response:
        body = response.read()
    if len(body) < 1000:
        raise ValueError("프레임 응답이 비어 있습니다")
    path = job_dir / f"frame-{index:03d}.jpg"
    path.write_bytes(body)
    return path


class Handler(BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        print(f"[calibration] {self.address_string()} - {fmt % args}")

    def send_json(self, value, status=200):
        body = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if urlparse(self.path).path in ("/", "/index.html"):
            body = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_error(404)

    def do_POST(self):
        if urlparse(self.path).path != "/api/calibrate":
            self.send_json({"ok": False, "error": "unknown endpoint"}, 404)
            return
        job_dir = RESULT_ROOT / uuid.uuid4().hex
        job_dir.mkdir(parents=True)
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length) or b"{}")
            target_frames = min(max(int(payload.get("frames", 15)), 6), 40)
            grabbed = attempts = 0
            while grabbed < target_frames and attempts < target_frames * 3:
                attempts += 1
                try:
                    grab_frame(job_dir, grabbed)
                    grabbed += 1
                    time.sleep(GRAB_INTERVAL_SEC)
                except (OSError, ValueError):
                    time.sleep(1.0)
            if grabbed == 0:
                raise RuntimeError("호모그래피 앱(:8001)에서 프레임을 하나도 받지 못했습니다")

            output_file = job_dir / "intrinsics.json"
            import subprocess
            result = subprocess.run(
                [str(TOOL), "calibrate-intrinsics", "--config", str(CONFIG),
                 "--images", str(job_dir), "--output", str(output_file)],
                capture_output=True, text=True, timeout=300, check=False)
            if result.returncode != 0:
                raise RuntimeError(result.stderr.strip() or "캘리브레이션 실패")

            value = json.loads(output_file.read_text(encoding="utf-8"))
            shutil.copyfile(output_file, OUTPUT_PATH)   # 운영 위치에 원자성 요치 없음(단순 복사)
            self.send_json({"ok": True, "result": value,
                            "saved_to": str(OUTPUT_PATH)})
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError,
                json.JSONDecodeError) as error:
            self.send_json({"ok": False, "error": str(error)}, 502)
        finally:
            shutil.rmtree(job_dir, ignore_errors=True)


if __name__ == "__main__":
    print(f"calibration-app listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
