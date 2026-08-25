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
import re
import shutil
import subprocess
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

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
# 산출물 보관 위치. 호모그래피 앱의 운영 H와 같은 루트를 공유하며,
# 채널별 파일(camera_intrinsics_<stream_id>.json)로 저장한다.
INTRINSICS_ROOT = Path(os.environ.get(
    "SAFETY_SERVER_HOMOGRAPHY_DIR", "/etc/forklift_safety/homography"))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)
SESSION_ROOT = RESULT_ROOT / "session"
SESSION_ROOT.mkdir(parents=True, exist_ok=True)


def session_dir(stream_id):
    path = SESSION_ROOT / stream_id
    path.mkdir(parents=True, exist_ok=True)
    return path


def output_path(stream_id):
    return INTRINSICS_ROOT / f"camera_intrinsics_{stream_id}.json" 

PAGE = """<!doctype html>
<html lang="ko"><meta charset="utf-8">
<title>카메라 캘리브레이션</title>
<style>
 body{font-family:system-ui,sans-serif;max-width:960px;margin:32px auto;padding:0 16px}
 button{font-size:1rem;padding:10px 22px;margin-right:8px}
 button:disabled{opacity:.4}
 pre{background:#0b1724;color:#d7e5f2;padding:12px;overflow:auto;white-space:pre-wrap}
 .hint{color:#556} #count{font-weight:700}
 .viewers{display:flex;gap:8px;margin-top:12px}
 .viewer{flex:1;background:#000;border:1px solid #333;aspect-ratio:2592/1520;
         display:flex;align-items:center;justify-content:center;position:relative}
 .viewer img{max-width:100%;max-height:100%}
 .viewer span{color:#5a6a7a;font-size:.9rem}
 .viewer b{position:absolute;top:6px;left:8px;color:#cfe0f0;font-size:.85rem;z-index:1}
 .thumbs{display:flex;flex-wrap:wrap;gap:4px;margin-top:8px}
 .thumbs img{width:110px;height:64px;object-fit:cover;background:#000;
             border:1px solid #444;cursor:pointer}
 .thumbs img:hover{border-color:#8ab4e0}
</style>
<h1>카메라 캘리브레이션</h1>
<p>
 <label>채널 <select id="stream"></select></label>
</p>
<p class="hint">ChArUco 보드(DICT_4X4_50 · 7×5 · 정사각 38mm · 마커 19mm)를 다양한 각도·위치로
두고 한 장씩 캡처하세요. 잘린 장은 자동으로 무시됩니다. 최소 6장, 권장 12장 이상.
채널별로 캘리브레이션 결과가 따로 저장됩니다.</p>
<p>
 <button id="shot">📷 캡처</button>
 <button id="reset">수집 초기화</button>
 <button id="run">캘리브레이션 실행</button>
 보드 인식 대상: <span id="count">0</span>장 <span id="status"></span>
</p>
<div class="viewers">
 <div class="viewer"><b>마지막 캡처</b><img id="preview" alt=""><span id="preview-empty"></span></div>
 <div class="viewer"><b>왜곡 보정 미리보기</b><img id="undistorted" alt=""><span>산출 후 결과가 표시됩니다</span></div>
</div>
<div class="thumbs" id="thumbs"></div>
<pre id="result">결과가 여기 표시됩니다.</pre>
<script>
const $ = (sel) => document.querySelector(sel);
const streamId = () => $('#stream').value;

async function loadStreams() {
  const value = await (await fetch('/api/streams')).json();
  $('#stream').innerHTML = (value.streams || []).map((s) =>
    `<option value="${s.stream_id}">채널 ${s.channel} · ${s.camera_id} (${s.image_width_px}×${s.image_height_px})</option>`).join('');
  await refresh();
}

async function refresh() {
  const sid = streamId();
  if (!sid) return;
  const value = await (await fetch(`/api/frames?stream_id=${encodeURIComponent(sid)}`)).json();
  $('#count').textContent = value.count;
  // 수집 히스토리 썸네일. 사진을 클릭하면 마지막 캡처 뷰어로 크게 볼 수 있다.
  $('#thumbs').innerHTML = (value.files || []).map((name) =>
    `<img loading="lazy" src="/api/frames/photo?stream_id=${encodeURIComponent(sid)}&name=${encodeURIComponent(name)}"
          title="${name}" onclick="$('#preview').src=this.src">`).join('');
}

$('#stream').onchange = async () => {
  $('#preview').src = '';
  await refresh();
};

$('#shot').onclick = async () => {
  $('#shot').disabled = true;
  try {
    const value = await (await fetch('/api/frames', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({stream_id: streamId()})})).json();
    if (!value.ok) throw new Error(value.error || 'HTTP 오류');
    await refresh();
  } catch (error) { $('#status').textContent = String(error); }
  finally { $('#shot').disabled = false; }
};

$('#reset').onclick = async () => {
  await fetch(`/api/frames?stream_id=${encodeURIComponent(streamId())}`, {method: 'DELETE'});
  $('#preview').src = '';
  $('#undistorted').src = '';
  $('#status').textContent = '';
  await refresh();
};

$('#run').onclick = async () => {
  $('#run').disabled = true;
  $('#status').textContent = '산출 중…';
  try {
    const response = await fetch('/api/calibrate', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({stream_id: streamId()})});
    const value = await response.json();
    if (!value.ok) throw new Error(value.error || 'HTTP ' + response.status);
    const r = value.result;
    $('#status').textContent = `완료 · 재투영 RMSE ${Number(r.reprojection_rmse_px).toFixed(3)} px`;
    $('#result').textContent = JSON.stringify(r, null, 2);
    if (value.undistorted_image) {
      $('#undistorted').src = 'data:image/jpeg;base64,' + value.undistorted_image;
    }
  } catch (error) {
    $('#status').textContent = '실패';
    $('#result').textContent = String(error);
  } finally { $('#run').disabled = false; }
};
loadStreams();
</script>"""


def grab_frame(stream_id):
    """스냅샷(profile1)에서 원본 JPEG 한 장을 받아 해당 스트림 세션 폴더에 쌓는다."""
    request = urllib.request.Request(
        f"{HOMOGRAPHY_BASE}/api/camera/frame?snapshot=1&stream_id={stream_id}")
    with urllib.request.urlopen(request, timeout=FRAME_TIMEOUT_SEC) as response:
        body = response.read()
    if len(body) < 1000:
        raise ValueError("프레임 응답이 비어 있습니다")
    job_dir = session_dir(stream_id)
    (job_dir / f"frame-{len(list(job_dir.glob('*.jpg'))):03d}.jpg").write_bytes(body)
    return body


def reset_session(stream_id):
    shutil.rmtree(session_dir(stream_id), ignore_errors=True)
    session_dir(stream_id)


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
        if path == "/api/streams":
            # 호모그래피 앱 상태의 스트림 목록을 그대로 중계한다(채널 선택용).
            with urllib.request.urlopen(f"{HOMOGRAPHY_BASE}/api/status",
                                        timeout=FRAME_TIMEOUT_SEC) as response:
                status = json.loads(response.read())
            self.send_json({"ok": True, "streams": status.get("streams", [])})
            return
        if path == "/api/frames":
            query = urlparse(self.path).query
            stream_id = parse_qs(query).get("stream_id", [""])[0]
            job_dir = session_dir(stream_id)
            files = sorted(path.name for path in job_dir.glob("frame-*.jpg"))
            self.send_json({"ok": True, "files": files})
            return
        if path == "/api/frames/photo":
            query = parse_qs(urlparse(self.path).query)
            stream_id = query.get("stream_id", [""])[0]
            name = query.get("name", [""])[0]
            # 경로 순회 방지: 수집기가 만든 frame-XXXX.jpg 이름만 허용한다.
            if not re.fullmatch(r"frame-\d{3}\.jpg", name):
                self.send_error(404)
                return
            photo = session_dir(stream_id) / name
            if not photo.is_file():
                self.send_error(404)
                return
            body = photo.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_error(404)

    def do_POST(self):
        action = urlparse(self.path).path
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length) or b"{}")
            stream_id = str(payload.get("stream_id") or "")
            if action in ("/api/frames", "/api/calibrate") and not stream_id:
                raise ValueError("stream_id가 필요합니다")
            job_dir = session_dir(stream_id)
            if action == "/api/frames":
                image = grab_frame(stream_id)
                del image
                self.send_json({"ok": True,
                                "count": len(list(job_dir.glob('*.jpg')))})
            elif action == "/api/calibrate":
                captured = len(list(job_dir.glob('*.jpg')))
                if captured == 0:
                    raise RuntimeError("캡처한 사진이 없습니다. 먼저 캡처하세요")
                output_file = job_dir / "intrinsics.json"
                preview_file = job_dir / "undistorted.jpg"
                first_frame = min(job_dir.glob("*.jpg"),
                                  key=lambda path: path.name, default=None)
                result = subprocess.run(
                    [str(TOOL), "calibrate-intrinsics", "--config", str(CONFIG),
                     "--images", str(job_dir), "--output", str(output_file),
                     "--preview", str(preview_file)] if first_frame else
                    [str(TOOL), "calibrate-intrinsics", "--config", str(CONFIG),
                     "--images", str(job_dir), "--output", str(output_file)],
                    capture_output=True, text=True, timeout=300, check=False)
                if result.returncode != 0:
                    raise RuntimeError(result.stderr.strip() or "캘리브레이션 실패")
                value = json.loads(output_file.read_text(encoding="utf-8"))
                destination = output_path(stream_id)
                temporary = destination.with_name(f".{destination.name}.{os.urandom(8).hex()}.tmp")
                shutil.copyfile(output_file, temporary)
                os.replace(temporary, destination)
                response = {"ok": True, "result": value, "stream_id": stream_id,
                            "saved_to": str(destination)}
                if preview_file.is_file():
                    # 왜곡 보정 전후를 나란히 비교해 사용자가 결과를 눈으로 검증하게 한다.
                    response["undistorted_image"] = base64.b64encode(
                        preview_file.read_bytes()).decode()
                self.send_json(response)
            else:
                self.send_json({"ok": False, "error": "unknown endpoint"}, 404)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError,
                json.JSONDecodeError) as error:
            self.send_json({"ok": False, "error": str(error)}, 502)

    def do_DELETE(self):
        if urlparse(self.path).path == "/api/frames":
            query = urlparse(self.path).query
            stream_id = parse_qs(query).get("stream_id", [""])[0]
            reset_session(stream_id)
            self.send_json({"ok": True})
            return
        self.send_error(404)


if __name__ == "__main__":
    print(f"calibration-app listening on {HOST}:{PORT}")
    print(f"calibration-app listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
