#!/usr/bin/env python3
"""카메라 내부 파라미터 캘리브레이션 앱 (포트 8002).

호모그래피 앱(:8001)의 스냅샷 경로로 CCTV 원본 프레임을 한 장씩 받아 ChArUco 보드로
카메라 캘리브레이션을 수행하고, 산출물(K, 왜곡계수 JSON)을 저장한다.

검출·수치는 homography_tool calibrate-intrinsics(C++, OpenCV)가 전담하며
이 서버는 수집·실행·표시만 한다.
"""

import base64
import json
import os
import shutil
import subprocess
import urllib.request
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

RESULT_ROOT = Path(os.environ.get("CALIBRATION_RESULT_DIR", "/tmp/calibration-results"))
# 산출물 보관 위치. 호모그래피 앱의 운영 H와 같은 루트를 공유한다.
OUTPUT_PATH = Path(os.environ.get(
    "CAMERA_INTRINSICS_PATH",
    str(Path(os.environ.get("SAFETY_SERVER_HOMOGRAPHY_DIR",
                            "/etc/forklift_safety/homography")) / "camera_intrinsics.json")))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)
SESSION_DIR = RESULT_ROOT / "session"
OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)

PAGE = """<!doctype html>
<html lang="ko"><meta charset="utf-8">
<title>카메라 캘리브레이션</title>
<style>
 body{font-family:system-ui,sans-serif;max-width:860px;margin:32px auto;padding:0 16px}
 button{font-size:1rem;padding:10px 22px;margin-right:8px}
 button:disabled{opacity:.4}
 pre{background:#0b1724;color:#d7e5f2;padding:12px;overflow:auto;white-space:pre-wrap}
 img{max-width:100%;border:1px solid #ccd6e0;background:#eee;min-height:40px}
 .hint{color:#556} #count{font-weight:700}
</style>
<h1>카메라 캘리브레이션</h1>
<p class="hint">ChArUco 보드(DICT_4X4_50 · 7×5 · 정사각 38mm · 마커 19mm)를 다양한 각도·위치로
두고 한 장씩 캡처하세요. 잘린 장은 자동으로 무시됩니다. 최소 6장, 권장 12장 이상.</p>
<p>
 <button id="shot">📷 캡처</button>
 <button id="reset">수집 초기화</button>
 <button id="run">캘리브레이션 실행</button>
 보드 인식 대상: <span id="count">0</span>장 <span id="status"></span>
</p>
<img id="preview" alt="마지막 캡처">
<pre id="result">결과가 여기 표시됩니다.</pre>
<script>
const $ = (sel) => document.querySelector(sel);
const refresh = async () => {
  const value = await (await fetch('/api/frames')).json();
  $('#count').textContent = value.count;
};
$('#shot').onclick = async () => {
  $('#shot').disabled = true;
  try {
    const value = await (await fetch('/api/frames', {method: 'POST'})).json();
    if (!value.ok) throw new Error(value.error || 'HTTP 오류');
    $('#preview').src = 'data:image/jpeg;base64,' + value.image;
    await refresh();
  } catch (error) { $('#status').textContent = String(error); }
  finally { $('#shot').disabled = false; }
};
$('#reset').onclick = async () => {
  await fetch('/api/frames', {method: 'DELETE'});
  $('#preview').src = '';
  $('#status').textContent = '';
  await refresh();
};
$('#run').onclick = async () => {
  $('#run').disabled = true;
  $('#status').textContent = '산출 중…';
  try {
    const response = await fetch('/api/calibrate', {method: 'POST'});
    const value = await response.json();
    if (!value.ok) throw new Error(value.error || 'HTTP ' + response.status);
    const r = value.result;
    $('#status').textContent = `완료 · 재투영 RMSE ${Number(r.reprojection_rmse_px).toFixed(3)} px`;
    $('#result').textContent = JSON.stringify(r, null, 2);
  } catch (error) {
    $('#status').textContent = '실패';
    $('#result').textContent = String(error);
  } finally { $('#run').disabled = false; }
};
refresh();
</script>
"""


def grab_frame():
    """스냅샷(profile1)에서 원본(2592x1520) JPEG 한 장을 받아 세션 폴더에 쌓는다."""
    request = urllib.request.Request(f"{HOMOGRAPHY_BASE}/api/camera/frame?snapshot=1")
    with urllib.request.urlopen(request, timeout=FRAME_TIMEOUT_SEC) as response:
        body = response.read()
    if len(body) < 1000:
        raise ValueError("프레임 응답이 비어 있습니다")
    path = SESSION_DIR / f"frame-{len(list(SESSION_DIR.glob('*.jpg'))):03d}.jpg"
    path.write_bytes(body)
    return body


def reset_session():
    shutil.rmtree(SESSION_DIR, ignore_errors=True)
    SESSION_DIR.mkdir(parents=True)


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
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            body = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/api/frames":
            self.send_json({"ok": True, "count": len(list(SESSION_DIR.glob('*.jpg')))})
            return
        self.send_error(404)

    def do_POST(self):
        action = urlparse(self.path).path
        try:
            if action == "/api/frames":
                image = grab_frame()
                self.send_json({"ok": True,
                                "count": len(list(SESSION_DIR.glob('*.jpg'))),
                                "image": base64.b64encode(image).decode()})
            elif action == "/api/calibrate":
                captured = len(list(SESSION_DIR.glob('*.jpg')))
                if captured == 0:
                    raise RuntimeError("캡처한 사진이 없습니다. 먼저 캡처하세요")
                output_file = SESSION_DIR / "intrinsics.json"
                result = subprocess.run(
                    [str(TOOL), "calibrate-intrinsics", "--config", str(CONFIG),
                     "--images", str(SESSION_DIR), "--output", str(output_file)],
                    capture_output=True, text=True, timeout=300, check=False)
                if result.returncode != 0:
                    raise RuntimeError(result.stderr.strip() or "캘리브레이션 실패")
                value = json.loads(output_file.read_text(encoding="utf-8"))
                temporary = OUTPUT_PATH.with_name(f".{OUTPUT_PATH.name}.{os.urandom(8).hex()}.tmp")
                shutil.copyfile(output_file, temporary)
                os.replace(temporary, OUTPUT_PATH)
                self.send_json({"ok": True, "result": value,
                                "saved_to": str(OUTPUT_PATH)})
            else:
                self.send_json({"ok": False, "error": "unknown endpoint"}, 404)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError,
                json.JSONDecodeError) as error:
            self.send_json({"ok": False, "error": str(error)}, 502)

    def do_DELETE(self):
        if urlparse(self.path).path == "/api/frames":
            reset_session()
            self.send_json({"ok": True})
            return
        self.send_error(404)


if __name__ == "__main__":
    reset_session()   # 서버 시작마다 깨끗한 세션
    print(f"calibration-app listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
