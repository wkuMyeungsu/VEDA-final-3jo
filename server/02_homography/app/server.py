#!/usr/bin/env python3
"""서버 도구를 호출하는 LAN용 웹 셸.

호모그래피 명령은 허용된 인자만 셸 없이 subprocess로 실행함.
"""
import json
import base64
import math
import mimetypes
import os
import stat
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
STREAM_CONFIG = ROOT.parent / "config" / "stream_config.json"
STREAM_CONFIG_VALUE = json.loads(STREAM_CONFIG.read_text(encoding="utf-8"))
OUTPUTS = CONFIG_VALUE.get("outputs", {})
COMMON_CONFIG_DIR = Path(os.environ.get(
    "SERVER_COMMON_CONFIG_DIR", str(ROOT.parents[1] / "config")))
CAMERA_LIST_CONFIG = COMMON_CONFIG_DIR / "camera_list.json"
CAMERA_LIST = json.loads(CAMERA_LIST_CONFIG.read_text(encoding="utf-8")) if CAMERA_LIST_CONFIG.is_file() else {}
CAMERA_MODEL_CONFIG = COMMON_CONFIG_DIR / "camera_model.json"
CAMERA_MODELS = json.loads(CAMERA_MODEL_CONFIG.read_text(encoding="utf-8")) if CAMERA_MODEL_CONFIG.is_file() else {}
HOST = os.environ.get("ADMIN_GUI_HOST", "0.0.0.0")
PORT = int(os.environ.get("HOMOGRAPHY_APP_PORT", "8001"))
TOOL = os.environ.get("HOMOGRAPHY_TOOL", "homography_tool")
TIMEOUT = int(os.environ.get("HOMOGRAPHY_COMMAND_TIMEOUT_SEC", "120"))
RESULT_ROOT = Path(os.environ.get("HOMOGRAPHY_RESULT_DIR", "/tmp/homography-results"))
RESULT_TTL_SEC = int(os.environ.get("ADMIN_GUI_RESULT_TTL_SEC", "3600"))
RESULT_ROOT.mkdir(parents=True, exist_ok=True)
OPERATIONAL_HOMOGRAPHY_ROOT = Path(os.environ.get(
    "SAFETY_SERVER_HOMOGRAPHY_DIR",
    str(COMMON_CONFIG_DIR / "homography")))
MIN_COMMON_MARKERS = int(CONFIG_VALUE.get("map", {}).get("min_common_markers", 3))
MAX_VERIFICATION_STREAMS = int(STREAM_CONFIG_VALUE.get("verification", {}).get("max_streams", 0))
if MAX_VERIFICATION_STREAMS < 2 or MAX_VERIFICATION_STREAMS > 32:
    raise ValueError(f"{STREAM_CONFIG}의 verification.max_streams는 2~32 범위여야 합니다")
CONFIG_LOCK = threading.RLock()


def configured_camera_entries():
    """공통 camera_list.json의 CCTV 목록을 검증해 반환한다."""
    cameras = CAMERA_LIST.get("cameras")
    if not isinstance(cameras, list) or not cameras:
        raise ValueError(f"{CAMERA_LIST_CONFIG}의 cameras는 비어 있지 않은 배열이어야 합니다")
    ids = set()
    for camera in cameras:
        if not isinstance(camera, dict):
            raise ValueError(f"{CAMERA_LIST_CONFIG}의 카메라 항목이 객체가 아닙니다")
        camera_id = str(camera.get("camera_id", "")).strip()
        model = str(camera.get("model", "")).strip()
        channels = camera.get("channels")
        if not camera_id or camera_id in ids or not model or not isinstance(channels, list):
            raise ValueError(f"{CAMERA_LIST_CONFIG}의 카메라 ID·모델·채널 설정이 잘못되었거나 중복됩니다")
        ids.add(camera_id)
    return cameras


def select_camera_entry(camera_id=None):
    """환경변수 또는 목록의 첫 번째 CCTV를 현재 보정 대상으로 선택한다."""
    cameras = configured_camera_entries()
    requested = str(camera_id or os.environ.get("HOMOGRAPHY_CAMERA_ID", "")).strip()
    if requested:
        for camera in cameras:
            if camera["camera_id"] == requested:
                return camera
        raise ValueError(f"{CAMERA_LIST_CONFIG}에 카메라가 없습니다: {requested}")
    return cameras[0]


def runtime_camera(camera_entry):
    """기존 연결 코드가 사용할 현재 카메라 런타임 형태를 만든다."""
    return {"camera": camera_entry, "connection": camera_entry.get("connection", {})}


CAMERA = runtime_camera(select_camera_entry())


def load_camera_model_channels():
    """로컬 camera_model.json에서 모델별 채널 수를 읽고 검증한다."""
    model_list = CAMERA_MODELS.get("models")
    if not isinstance(model_list, list) or not model_list:
        raise ValueError(f"{CAMERA_MODEL_CONFIG}의 models는 비어 있지 않은 배열이어야 합니다")

    model_channels = {}
    for item in model_list:
        if not isinstance(item, dict):
            raise ValueError(f"{CAMERA_MODEL_CONFIG}의 모델 항목이 객체가 아닙니다")
        model_name = str(item.get("model", "")).strip()
        try:
            channel_count = int(item.get("channel_count"))
        except (TypeError, ValueError) as error:
            raise ValueError(f"{CAMERA_MODEL_CONFIG}의 {model_name or '모델'} channel_count가 올바르지 않습니다") from error
        if not model_name or channel_count < 1 or model_name in model_channels:
            raise ValueError(f"{CAMERA_MODEL_CONFIG}의 모델명 또는 channel_count가 잘못되었거나 중복됩니다")
        # 현재 시스템은 CCTV 한 대당 최대 4개 채널까지 지원한다.
        # 실제 활성 채널 수의 기준은 이 모델 파일이다.
        if channel_count > 4:
            raise ValueError(f"{CAMERA_MODEL_CONFIG}의 {model_name} channel_count는 현재 4 이하만 지원합니다")
        model_channels[model_name] = channel_count
    return model_channels


def load_camera_channel_count(runtime_config=None):
    """현재 카메라 설정의 모델명으로 지원 채널 수를 결정한다."""
    model_channels = load_camera_model_channels()

    camera = (runtime_config or CAMERA).get("camera")
    if not isinstance(camera, dict):
        raise ValueError(f"{CAMERA_LIST_CONFIG}의 현재 카메라 객체가 없습니다")
    camera_model = str(camera.get("model", "")).strip()
    if camera_model not in model_channels:
        raise ValueError(
            f"{CAMERA_LIST_CONFIG}의 camera.model '{camera_model}'이 "
            f"{CAMERA_MODEL_CONFIG}에 정의되어 있지 않습니다")
    return camera_model, model_channels[camera_model]


CAMERA_MODEL, MAX_SUPPORTED_CHANNELS = load_camera_channel_count()
if MIN_COMMON_MARKERS < 3:
    raise ValueError("map.min_common_markers는 3 이상이어야 합니다")
CAMERA_RETRY_DELAY_SEC = 0.5
LIVE_CAMERA_ROOT = RESULT_ROOT / "live-camera"
LIVE_CAMERA_ROOT.mkdir(parents=True, exist_ok=True)


def configured_output_name(key, fallback):
    """설정된 결과 파일명을 경로 없이 반환함."""
    value = OUTPUTS.get(key, fallback)
    return Path(str(value)).name or fallback


def configured_camera_id():
    """산출물을 저장할 물리 CCTV ID를 안전하게 읽는다."""
    # H 파일은 실제 CCTV별로 보관한다. 카메라 ID에 경로 문자가 들어가면
    # 저장 위치가 설정 디렉터리 밖으로 빠질 수 있으므로 단순 식별자만 허용한다.
    value = str(CAMERA.get("camera", {}).get("camera_id") or
                os.environ.get("HOMOGRAPHY_CAMERA_ID", "CAM_01")).strip()
    if not value or any(character not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
                         for character in value):
        raise ValueError("camera.camera_id는 영문/숫자/_/-만 사용할 수 있습니다")
    return value


def camera_status_entries():
    """화면에 보여줄 CCTV 목록을 모델 채널 수와 함께 만든다."""
    model_channels = load_camera_model_channels()
    entries = []
    for camera in configured_camera_entries():
        model = str(camera.get("model", ""))
        entries.append({
            "camera_id": camera["camera_id"],
            "camera_model": model,
            "channel_count": model_channels.get(model, 0),
            "configured_channels": sorted(
                int(item["channel"]) for item in camera.get("channels", [])
                if isinstance(item, dict) and str(item.get("channel", "")).isdigit()
            ),
        })
    return entries


def stream_id_for(camera_id, channel):
    """물리 CCTV와 내부 채널을 서버 전체에서 유일한 키로 합친다."""
    return f"{camera_id}_CH_{int(channel):02d}"


def configured_stream_entries():
    """camera_list.json의 모든 CCTV×채널을 보정 대상 스트림으로 펼친다."""
    model_channels = load_camera_model_channels()
    streams = []
    seen = set()
    for camera in configured_camera_entries():
        camera_id = str(camera["camera_id"])
        model = str(camera["model"])
        channel_limit = model_channels.get(model)
        if channel_limit is None:
            raise ValueError(f"지원하지 않는 카메라 모델입니다: {model}")
        for item in camera.get("channels", []):
            if not isinstance(item, dict):
                continue
            try:
                channel = int(item.get("channel"))
            except (TypeError, ValueError):
                continue
            if channel < 1 or channel > channel_limit:
                continue
            stream_id = stream_id_for(camera_id, channel)
            if stream_id in seen:
                raise ValueError(f"stream_id가 중복됩니다: {stream_id}")
            seen.add(stream_id)
            streams.append({
                "stream_id": stream_id,
                "camera_id": camera_id,
                "camera_model": model,
                "channel": channel,
                "image_width_px": int(item.get("image_width_px", 0) or 0),
                "image_height_px": int(item.get("image_height_px", 0) or 0),
                "camera": camera,
                "channel_config": item,
            })
    if not streams:
        raise ValueError(f"{CAMERA_LIST_CONFIG}에 사용할 수 있는 스트림이 없습니다")
    return streams


def configured_stream(stream_id=None, camera_id=None, channel=None):
    """요청한 stream_id를 검증하고 CCTV×채널 설정을 반환한다."""
    entries = configured_stream_entries()
    if stream_id:
        wanted = str(stream_id).strip()
        match = next((entry for entry in entries if entry["stream_id"] == wanted), None)
        if match is None:
            raise ValueError(f"등록된 스트림이 없습니다: {wanted}")
        return match
    selected_camera = str(camera_id or configured_camera_id()).strip()
    try:
        selected_channel = int(channel)
    except (TypeError, ValueError) as error:
        raise ValueError("channel이 올바르지 않습니다") from error
    match = next((entry for entry in entries
                  if entry["camera_id"] == selected_camera and entry["channel"] == selected_channel), None)
    if match is None:
        raise ValueError(f"{selected_camera} 채널 {selected_channel} 설정이 없습니다")
    return match


def stream_status_entries():
    """UI가 한 번에 선택할 수 있도록 모든 스트림을 평탄화한다."""
    result = []
    for entry in configured_stream_entries():
        result.append({key: entry[key] for key in (
            "stream_id", "camera_id", "camera_model", "channel",
            "image_width_px", "image_height_px")})
    return result


def atomic_write_text(path, text, default_mode=0o644):
    """파일을 같은 폴더의 임시 파일로 쓴 뒤 안전하게 교체한다."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else default_mode
    temporary_path = path.parent / f".{path.name}.{uuid.uuid4().hex}.tmp"
    try:
        temporary_path.write_text(text, encoding="utf-8")
        os.chmod(temporary_path, mode)
        os.replace(temporary_path, path)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass


def save_camera_settings(camera_id, camera_model):
    """공통 camera_list.json의 현재 CCTV 이름·모델 매핑을 저장한다."""
    global CAMERA_LIST, CAMERA, CAMERA_MODEL, MAX_SUPPORTED_CHANNELS

    camera_id = str(camera_id or "").strip()
    camera_model = str(camera_model or "").strip()
    if not 1 <= len(camera_id) <= 64 or any(
            character not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
            for character in camera_id):
        raise ValueError("카메라명은 영문/숫자/_/-로 1~64자까지 입력해야 합니다")

    model_channels = load_camera_model_channels()
    if camera_model not in model_channels:
        raise ValueError(f"지원하지 않는 카메라 모델입니다: {camera_model}")

    with CONFIG_LOCK:
        updated_list = json.loads(json.dumps(CAMERA_LIST))
        cameras = updated_list.get("cameras")
        if not isinstance(cameras, list) or not cameras:
            raise ValueError(f"{CAMERA_LIST_CONFIG}의 cameras가 없습니다")

        current_id = configured_camera_id()
        current_index = next((index for index, item in enumerate(cameras)
                              if item.get("camera_id") == current_id), None)
        target_index = next((index for index, item in enumerate(cameras)
                             if item.get("camera_id") == camera_id), None)
        if target_index is None:
            if current_index is None:
                raise ValueError(f"현재 카메라를 {camera_id}로 저장할 수 없습니다")
            target_index = current_index
        elif target_index != current_index:
            # 이미 목록에 있는 카메라를 선택한 경우에는 그 항목을 활성화한다.
            # 다른 카메라의 네트워크·채널 설정은 건드리지 않는다.
            pass

        cameras[target_index]["camera_id"] = camera_id
        cameras[target_index]["model"] = camera_model
        updated_camera = cameras[target_index]

        # 계정 정보가 들어갈 수 있는 공통 목록은 기존 파일 권한을 보존한다.
        atomic_write_text(
            CAMERA_LIST_CONFIG,
            json.dumps(updated_list, ensure_ascii=False, indent=2) + "\n",
            default_mode=0o600,
        )

        CAMERA_LIST = updated_list
        CAMERA = runtime_camera(updated_camera)
        CAMERA_MODEL = camera_model
        MAX_SUPPORTED_CHANNELS = model_channels[camera_model]

        # 현재 모델의 RTSP 채널 수가 바뀌면 기존 worker를 끊고 다음 캡처부터
        # 새 카메라명·모델 설정으로 다시 연결한다.
        if "CAMERA_STREAM" in globals():
            CAMERA_STREAM.stop()
        if "CAMERA_DETECTION_STREAM" in globals():
            CAMERA_DETECTION_STREAM.stop()

    return {
        "camera_id": camera_id,
        "camera_model": camera_model,
        "channel_count": model_channels[camera_model],
        "camera_list_path": str(CAMERA_LIST_CONFIG),
        "camera_model_path": str(CAMERA_MODEL_CONFIG),
        "homography_root": str(OPERATIONAL_HOMOGRAPHY_ROOT),
    }


def make_operational_homography(value):
    """전체 산출 결과에서 안전 서버용 최소 H만 추린다."""
    if not isinstance(value, dict):
        raise ValueError("호모그래피 결과가 JSON 객체가 아닙니다")

    try:
        channel = int(value.get("channel"))
    except (TypeError, ValueError) as error:
        raise ValueError("호모그래피 결과에 올바른 channel이 없습니다") from error
    camera_id = str(value.get("camera_id", "")).strip()
    stream_id = str(value.get("stream_id", "")).strip()
    if not camera_id or not stream_id:
        raise ValueError("호모그래피 결과에 camera_id와 stream_id가 필요합니다")
    stream = configured_stream(stream_id=stream_id)
    if stream["camera_id"] != camera_id or stream["channel"] != channel:
        raise ValueError("호모그래피 결과의 camera_id/stream_id/channel 조합이 camera_list와 다릅니다")
    if str(value.get("world_unit")) != "mm":
        raise ValueError("호모그래피 world_unit은 mm이어야 합니다")

    matrix = value.get("H_pixel_to_world")
    if not isinstance(matrix, list) or len(matrix) != 3 or any(
            not isinstance(row, list) or len(row) != 3 for row in matrix):
        raise ValueError("H_pixel_to_world는 3x3 행렬이어야 합니다")
    if any(not isinstance(cell, (int, float)) or isinstance(cell, bool) or
           not math.isfinite(float(cell)) for row in matrix for cell in row):
        raise ValueError("H_pixel_to_world에 유효하지 않은 숫자가 있습니다")

    image_size = value.get("image_size")
    if not isinstance(image_size, dict):
        raise ValueError("image_size가 없습니다")
    try:
        image_width = int(image_size["width"])
        image_height = int(image_size["height"])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("image_size의 width/height가 올바르지 않습니다") from error
    if image_width < 1 or image_height < 1:
        raise ValueError("image_size의 width/height는 1 이상이어야 합니다")

    # 이 객체만 운영 파일에 기록한다. RMSE·마커 목록·임시 경로·RTSP 정합
    # 정보는 작업 결과 파일에는 남지만 안전 서버 운영 파일에는 넣지 않는다.
    return {
        "schema_version": int(value.get("schema_version", 2)),
        "world_unit": "mm",
        "camera_id": camera_id,
        "stream_id": stream_id,
        "channel": channel,
        "H_pixel_to_world": matrix,
        "image_size": {"width": image_width, "height": image_height},
    }


def save_operational_homography(value):
    """최소 H를 운영 경로에 새로 만들거나 원자적으로 덮어쓴다.

    계산 결과는 먼저 임시 작업 폴더에 생성된다. 이 함수가 최종 저장을
    전담해, 파일이 없으면 생성하고 이미 있으면 같은 이름으로 교체한다.
    임시 파일도 같은 폴더에 만들어 중간 상태의 JSON이 노출되지 않게 한다.
    """
    operational_value = make_operational_homography(value)
    stream_id = operational_value["stream_id"]
    channel = operational_value["channel"]
    camera_homography_dir = OPERATIONAL_HOMOGRAPHY_ROOT / operational_value["camera_id"]
    # 카메라 폴더가 없어도 산출 버튼을 누르면 자동으로 만든다.
    camera_homography_dir.mkdir(parents=True, exist_ok=True)

    operational_file = camera_homography_dir / f"homography_channel_{channel}_mm.json"
    existed = operational_file.is_file()
    temporary_file = camera_homography_dir / (
        f".{operational_file.name}.{uuid.uuid4().hex}.tmp")
    serialized = json.dumps(operational_value, ensure_ascii=False, indent=2) + "\n"

    try:
        # 최종 파일과 같은 디렉터리에서 교체해야 원자적 덮어쓰기가 보장된다.
        temporary_file.write_text(serialized, encoding="utf-8")
        os.replace(temporary_file, operational_file)

        # 저장 직후 실제 파일을 다시 읽어 저장 성공을 확인한다.
        saved_value = json.loads(operational_file.read_text(encoding="utf-8"))
        if saved_value != operational_value:
            raise ValueError(f"저장 후 검증 실패: {operational_file}")
    finally:
        try:
            temporary_file.unlink()
        except FileNotFoundError:
            pass

    return {
        "path": str(operational_file),
        "created": not existed,
        "overwritten": existed,
        "action": "overwrite" if existed else "create",
    }


def load_operational_homography(stream_id):
    """기존 최종 H가 있으면 읽어 세션 중간 계산에만 사용한다."""
    stream = configured_stream(stream_id=stream_id)
    path = (OPERATIONAL_HOMOGRAPHY_ROOT / stream["camera_id"] /
            f"homography_channel_{stream['channel']}_mm.json")
    if not path.is_file():
        return None
    try:
        return make_operational_homography(json.loads(path.read_text(encoding="utf-8")))
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(f"기존 최종 H를 읽을 수 없습니다: {path}: {error}") from error


def matrix_multiply(left, right):
    """3×3 호모그래피 두 개를 곱하고 마지막 원소로 정규화한다."""
    result = [[sum(float(left[row][k]) * float(right[k][column]) for k in range(3))
               for column in range(3)] for row in range(3)]
    scale = result[2][2]
    if not math.isfinite(scale) or abs(scale) < 1e-12:
        raise ValueError("호모그래피 합성 결과가 유효하지 않습니다")
    return [[value / scale for value in row] for row in result]


def invert_matrix(matrix):
    """외부 수치 라이브러리 없이 3×3 행렬을 역행렬로 바꾼다."""
    augmented = [[float(matrix[row][column]) for column in range(3)] +
                 [1.0 if row == column else 0.0 for column in range(3)]
                 for row in range(3)]
    for column in range(3):
        pivot = max(range(column, 3), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-12:
            raise ValueError("호모그래피 역행렬을 계산할 수 없습니다")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(3):
            if row == column: continue
            factor = augmented[row][column]
            augmented[row] = [augmented[row][index] - factor * augmented[column][index]
                              for index in range(6)]
    inverse = [row[3:] for row in augmented]
    return inverse


def transform_point(matrix, point):
    """3×3 호모그래피로 한 점을 변환한다."""
    x, y = float(point["x"]), float(point["y"])
    denominator = matrix[2][0] * x + matrix[2][1] * y + matrix[2][2]
    if abs(denominator) < 1e-12:
        raise ValueError("호모그래피 변환 분모가 0에 가깝습니다")
    return {
        "x": (matrix[0][0] * x + matrix[0][1] * y + matrix[0][2]) / denominator,
        "y": (matrix[1][0] * x + matrix[1][1] * y + matrix[1][2]) / denominator,
    }


def image_world_bounds(matrix, image_size):
    """한 채널의 네 영상 모서리를 전체 맵 mm 좌표로 바꿔 범위를 구한다."""
    try:
        width = float(image_size["width"])
        height = float(image_size["height"])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("정합 검증용 image_size가 올바르지 않습니다") from error
    corners = [
        transform_point(matrix, {"x": 0, "y": 0}),
        transform_point(matrix, {"x": width, "y": 0}),
        transform_point(matrix, {"x": width, "y": height}),
        transform_point(matrix, {"x": 0, "y": height}),
    ]
    return {
        "min_x": min(point["x"] for point in corners),
        "max_x": max(point["x"] for point in corners),
        "min_y": min(point["y"] for point in corners),
        "max_y": max(point["y"] for point in corners),
    }


def union_world_bounds(bounds_list):
    """여러 채널의 전체 맵 범위를 하나의 표시 범위로 합친다."""
    if not bounds_list:
        raise ValueError("정합 검증용 맵 범위가 없습니다")
    min_x = min(bounds["min_x"] for bounds in bounds_list)
    max_x = max(bounds["max_x"] for bounds in bounds_list)
    min_y = min(bounds["min_y"] for bounds in bounds_list)
    max_y = max(bounds["max_y"] for bounds in bounds_list)
    margin = max(max_x - min_x, max_y - min_y, 1.0) * 0.03
    return {
        "min_x": min_x - margin,
        "max_x": max_x + margin,
        "min_y": min_y - margin,
        "max_y": max_y + margin,
    }


def solve_point_homography(source_points, destination_points):
    """공통 마커 꼭짓점으로 source 평면→destination 평면 H를 계산한다."""
    if len(source_points) != len(destination_points) or len(source_points) < 4:
        raise ValueError("채널 정합에는 최소 네 개의 대응점이 필요합니다")
    normal = [[0.0] * 8 for _ in range(8)]
    target = [0.0] * 8
    rows = []
    values = []
    for source, destination in zip(source_points, destination_points):
        x, y = source["x"], source["y"]
        u, v = destination["x"], destination["y"]
        rows.extend([
            [x, y, 1.0, 0, 0, 0, -u * x, -u * y],
            [0, 0, 0, x, y, 1.0, -v * x, -v * y],
        ])
        values.extend([u, v])
    for row, value in zip(rows, values):
        for left in range(8):
            target[left] += row[left] * value
            for right in range(8):
                normal[left][right] += row[left] * row[right]
    augmented = [normal[row] + [target[row]] for row in range(8)]
    for column in range(8):
        pivot = max(range(column, 8), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-10:
            raise ValueError("공통 마커가 일직선에 몰려 채널 정합을 계산할 수 없습니다")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(8):
            if row == column: continue
            factor = augmented[row][column]
            augmented[row] = [augmented[row][index] - factor * augmented[column][index]
                              for index in range(9)]
    parameters = [augmented[row][8] for row in range(8)]
    return [[parameters[0], parameters[1], parameters[2]],
            [parameters[3], parameters[4], parameters[5]],
            [parameters[6], parameters[7], 1.0]]


def capture_marker_corners(job_dir, result):
    """캡처 당시 검출 코너에 사용자가 보정한 코너를 덮어쓴다."""
    marker_file = job_dir / "markers.json"
    if not marker_file.is_file():
        raise ValueError(f"마커 검출 결과가 없습니다: {marker_file}")
    detected = json.loads(marker_file.read_text(encoding="utf-8"))
    # CLI 결과에는 layout.json이 그대로 복사되지 않을 수 있다. 따라서
    # 정합할 때는 캡처 폴더의 원본 layout.json을 우선 읽어야 사용자가
    # 화면에서 드래그한 꼭짓점과 제외 ID가 빠지지 않는다.
    layout_file = job_dir / "layout.json"
    layout = json.loads(layout_file.read_text(encoding="utf-8")) if layout_file.is_file() else result.get("layout", {})
    overrides = layout.get("corner_overrides", {})
    excluded = {int(value) for value in layout.get("excluded_ids", [])}
    markers = {}
    for marker_id, corners in zip(detected.get("ids", []), detected.get("corners", [])):
        if int(marker_id) in excluded:
            continue
        key = str(marker_id)
        points = overrides.get(key, corners) if isinstance(overrides, dict) else corners
        if not isinstance(points, list) or len(points) != 4:
            continue
        markers[int(marker_id)] = [
            {"x": float(point["x"]), "y": float(point["y"])} for point in points]
    return markers


def align_local_channel(source_result, source_corners, destination_result, destination_corners):
    """두 채널의 로컬 mm 좌표를 공통 마커로 연결한다."""
    source_ids = set(source_corners)
    destination_ids = set(destination_corners)
    common_ids = sorted(source_ids & destination_ids)
    if len(common_ids) < MIN_COMMON_MARKERS:
        raise ValueError(
            f"공통 마커가 {len(common_ids)}개입니다. "
            f"채널 정합에는 {MIN_COMMON_MARKERS}개 이상 필요합니다")
    source_h = source_result.get("H_pixel_to_world")
    destination_h = destination_result.get("H_pixel_to_world")
    if source_h is None or destination_h is None:
        raise ValueError("채널 로컬 호모그래피가 없습니다")
    source_points, destination_points = [], []
    for marker_id in common_ids:
        for source, destination in zip(source_corners[marker_id], destination_corners[marker_id]):
            source_points.append(transform_point(source_h, source))
            destination_points.append(transform_point(destination_h, destination))
    # 공통 마커가 한 직선에만 놓이면 화면상으로는 여러 개여도
    # 회전·기울기·원근을 안정적으로 결정할 수 없다. 중심점 삼각형의
    # 최대 면적을 확인해 방향 정보가 충분히 퍼져 있는지 먼저 검사한다.
    source_centers = []
    destination_centers = []
    for marker_id in common_ids:
        source_world = [transform_point(source_h, point) for point in source_corners[marker_id]]
        destination_world = [transform_point(destination_h, point) for point in destination_corners[marker_id]]
        source_centers.append({
            "x": sum(point["x"] for point in source_world) / 4.0,
            "y": sum(point["y"] for point in source_world) / 4.0,
        })
        destination_centers.append({
            "x": sum(point["x"] for point in destination_world) / 4.0,
            "y": sum(point["y"] for point in destination_world) / 4.0,
        })

    def maximum_triangle_area(points):
        maximum = 0.0
        for first in range(len(points)):
            for second in range(first + 1, len(points)):
                for third in range(second + 1, len(points)):
                    area = abs(
                        (points[second]["x"] - points[first]["x"]) *
                        (points[third]["y"] - points[first]["y"]) -
                        (points[second]["y"] - points[first]["y"]) *
                        (points[third]["x"] - points[first]["x"])
                    ) / 2.0
                    maximum = max(maximum, area)
        return maximum

    source_span = max(
        max(point["x"] for point in source_centers) - min(point["x"] for point in source_centers),
        max(point["y"] for point in source_centers) - min(point["y"] for point in source_centers),
        1.0,
    )
    destination_span = max(
        max(point["x"] for point in destination_centers) - min(point["x"] for point in destination_centers),
        max(point["y"] for point in destination_centers) - min(point["y"] for point in destination_centers),
        1.0,
    )
    if maximum_triangle_area(source_centers) < source_span * source_span * 1e-6 or \
            maximum_triangle_area(destination_centers) < destination_span * destination_span * 1e-6:
        raise ValueError("공통 마커가 한 직선에 몰려 채널 방향을 안정적으로 정합할 수 없습니다")
    transform = solve_point_homography(source_points, destination_points)
    squared = 0.0
    for source, destination in zip(source_points, destination_points):
        projected = transform_point(transform, source)
        squared += (projected["x"] - destination["x"]) ** 2 + (projected["y"] - destination["y"]) ** 2
    rmse = math.sqrt(squared / len(source_points))
    return transform, common_ids, rmse


def solve_linear_system(matrix, vector):
    """작은 정방 행렬을 가우스-조던 소거로 푼다."""
    size = len(vector)
    if len(matrix) != size or any(len(row) != size for row in matrix):
        raise ValueError("전역 정합 선형 시스템의 크기가 올바르지 않습니다")
    augmented = [[float(value) for value in matrix[row]] + [float(vector[row])]
                 for row in range(size)]
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-12:
            raise ValueError("전역 정합 선형 시스템이 특이합니다")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(size):
            if row == column:
                continue
            factor = augmented[row][column]
            if abs(factor) < 1e-18:
                continue
            augmented[row] = [augmented[row][index] - factor * augmented[column][index]
                              for index in range(size + 1)]
    return [augmented[row][size] for row in range(size)]


def homography_parameters(matrix):
    """마지막 원소를 1로 고정한 3×3 H를 최적화용 8개 값으로 펼친다."""
    normalized = [[float(value) for value in row] for row in matrix]
    scale = normalized[2][2]
    if abs(scale) < 1e-12:
        raise ValueError("전역 정합 초기 H를 정규화할 수 없습니다")
    return [normalized[row][column] / scale
            for row in range(3) for column in range(3)
            if not (row == 2 and column == 2)]


def parameters_homography(parameters):
    """최적화용 8개 값으로 정규화된 3×3 H를 만든다."""
    if len(parameters) != 8:
        raise ValueError("호모그래피 최적화 파라미터가 8개가 아닙니다")
    return [
        [parameters[0], parameters[1], parameters[2]],
        [parameters[3], parameters[4], parameters[5]],
        [parameters[6], parameters[7], 1.0],
    ]


def project_numeric(matrix, point):
    """숫자 배열 H로 mm 점을 변환한다. 최적화 루프에서 사용한다."""
    x, y = float(point["x"]), float(point["y"])
    denominator = matrix[2][0] * x + matrix[2][1] * y + matrix[2][2]
    if abs(denominator) < 1e-12:
        raise ValueError("전역 정합 중 H 변환 분모가 0에 가깝습니다")
    return (
        (matrix[0][0] * x + matrix[0][1] * y + matrix[0][2]) / denominator,
        (matrix[1][0] * x + matrix[1][1] * y + matrix[1][2]) / denominator,
    )


def global_alignment_residuals(parameters_by_stream, variable_streams, edges):
    """모든 CCTV×채널 쌍의 공통 마커 코너 오차를 하나의 벡터로 만든다."""
    transforms = {stream_id: parameters_homography(parameters)
                  for stream_id, parameters in parameters_by_stream.items()}
    residuals = []
    for edge in edges:
        source_h = transforms[edge["source_stream_id"]]
        destination_h = transforms[edge["destination_stream_id"]]
        for source, destination in zip(edge["source_points"], edge["destination_points"]):
            source_world = project_numeric(source_h, source)
            destination_world = project_numeric(destination_h, destination)
            residuals.extend((source_world[0] - destination_world[0],
                              source_world[1] - destination_world[1]))
    return residuals


def optimize_global_transforms(initial_transforms, anchor_stream_id, edges):
    """기준 스트림을 고정하고 모든 스트림 H를 공통 마커 오차로 동시에 최적화한다."""
    streams = sorted(initial_transforms)
    variable_streams = [stream_id for stream_id in streams if stream_id != anchor_stream_id]
    parameters_by_stream = {
        stream_id: homography_parameters(matrix)
        for stream_id, matrix in initial_transforms.items()
    }
    if not variable_streams:
        return {stream_id: parameters_homography(parameters)
                for stream_id, parameters in parameters_by_stream.items()}

    def residuals():
        return global_alignment_residuals(parameters_by_stream, variable_streams, edges)

    def cost(values):
        return sum(value * value for value in values)

    residual = residuals()
    current_cost = cost(residual)
    variable_count = len(variable_streams) * 8
    damping = 1e-3
    for _ in range(80):
        jacobian = [[0.0] * variable_count for _ in residual]
        parameter_index = 0
        for stream_id in variable_streams:
            parameters = parameters_by_stream[stream_id]
            for local_index in range(8):
                original = parameters[local_index]
                # 투영 성분은 수치 규모가 작아도 변환 결과에 큰 영향을 줄 수 있어
                # 선형 항과 원근 항에 서로 다른 미소 이동량을 사용한다.
                step = 1e-6 if local_index < 6 else 1e-9
                parameters[local_index] = original + step
                shifted = residuals()
                parameters[local_index] = original
                for row in range(len(residual)):
                    jacobian[row][parameter_index] = (shifted[row] - residual[row]) / step
                parameter_index += 1

        normal = [[0.0] * variable_count for _ in range(variable_count)]
        gradient = [0.0] * variable_count
        for row, value in zip(jacobian, residual):
            for left in range(variable_count):
                gradient[left] += row[left] * value
                for right in range(variable_count):
                    normal[left][right] += row[left] * row[right]
        for diagonal in range(variable_count):
            normal[diagonal][diagonal] += damping * max(1.0, normal[diagonal][diagonal])
        try:
            delta = solve_linear_system(normal, [-value for value in gradient])
        except ValueError:
            break
        if not all(math.isfinite(value) for value in delta):
            break

        parameter_index = 0
        for stream_id in variable_streams:
            for local_index in range(8):
                parameters_by_stream[stream_id][local_index] += delta[parameter_index]
                parameter_index += 1
        candidate = residuals()
        candidate_cost = cost(candidate)
        if math.isfinite(candidate_cost) and candidate_cost < current_cost:
            residual = candidate
            improvement = current_cost - candidate_cost
            current_cost = candidate_cost
            damping = max(1e-12, damping * 0.3)
            if max(abs(value) for value in delta) < 1e-8 or improvement < 1e-8:
                break
        else:
            parameter_index = 0
            for stream_id in variable_streams:
                for local_index in range(8):
                    parameters_by_stream[stream_id][local_index] -= delta[parameter_index]
                    parameter_index += 1
            damping = min(1e12, damping * 10.0)
    return {stream_id: parameters_homography(parameters)
            for stream_id, parameters in parameters_by_stream.items()}


def align_all_streams(stream_results, stream_corners, anchor_stream_id):
    """모든 CCTV×채널 스트림을 하나의 전체 맵 좌표계로 동시에 정합한다."""
    streams = sorted(stream_results)
    if anchor_stream_id not in streams:
        raise ValueError("전체 맵 기준 stream_id가 정합 대상에 없습니다")
    if len(streams) < 2:
        raise ValueError("전체 스트림 정합에는 최소 두 스트림이 필요합니다")

    edges = []
    adjacency = {stream_id: [] for stream_id in streams}
    skipped_pairs = []
    for source_index, source_stream_id in enumerate(streams):
        for destination_stream_id in streams[source_index + 1:]:
            try:
                transform, common_ids, pair_rmse = align_local_channel(
                    stream_results[source_stream_id], stream_corners[source_stream_id],
                    stream_results[destination_stream_id], stream_corners[destination_stream_id])
            except ValueError as error:
                # 공통 마커 부족·일직선 배치는 해당 연결만 제외한다.
                # 다른 스트림 연결로 전체 그래프가 이어지면 전역 정합은 계속한다.
                skipped_pairs.append({"stream_ids": [source_stream_id, destination_stream_id],
                                      "reason": str(error)})
                continue
            source_points = []
            destination_points = []
            for marker_id in common_ids:
                for source, destination in zip(
                        stream_corners[source_stream_id][marker_id],
                        stream_corners[destination_stream_id][marker_id]):
                    source_points.append(transform_point(
                        stream_results[source_stream_id]["H_pixel_to_world"], source))
                    destination_points.append(transform_point(
                        stream_results[destination_stream_id]["H_pixel_to_world"], destination))
            edge = {
                "source_stream_id": source_stream_id,
                "destination_stream_id": destination_stream_id,
                "transform_source_to_destination": transform,
                "common_ids": common_ids,
                "source_points": source_points,
                "destination_points": destination_points,
                "pair_rmse_mm": pair_rmse,
            }
            edges.append(edge)
            adjacency[source_stream_id].append((destination_stream_id, transform))
            adjacency[destination_stream_id].append((source_stream_id, invert_matrix(transform)))

    if not edges:
        raise ValueError("공통 마커가 충분한 스트림 연결이 없어 전체 맵을 만들 수 없습니다")

    # 기준 스트림에서 그래프를 따라가 초기값을 만든다. 이 값은 최종 답이
    # 아니라, 모든 엣지를 동시에 최적화하기 위한 시작점이다.
    transforms = {anchor_stream_id: [[1.0, 0.0, 0.0],
                                   [0.0, 1.0, 0.0],
                                   [0.0, 0.0, 1.0]]}
    pending = [anchor_stream_id]
    while pending:
        source_stream_id = pending.pop(0)
        for destination_stream_id, source_to_destination in adjacency[source_stream_id]:
            if destination_stream_id in transforms:
                continue
            transforms[destination_stream_id] = matrix_multiply(
                transforms[source_stream_id], invert_matrix(source_to_destination))
            pending.append(destination_stream_id)
    disconnected = [stream_id for stream_id in streams if stream_id not in transforms]
    if disconnected:
        raise ValueError(
            "전체 맵에 연결되지 않은 스트림: " + ", ".join(disconnected) +
            ". 공통 마커가 충분한 연결을 추가하세요.")

    transforms = optimize_global_transforms(transforms, anchor_stream_id, edges)
    total_squared = 0.0
    total_count = 0
    edge_results = []
    for edge in edges:
        squared = 0.0
        for source, destination in zip(edge["source_points"], edge["destination_points"]):
            source_world = transform_point(transforms[edge["source_stream_id"]], source)
            destination_world = transform_point(transforms[edge["destination_stream_id"]], destination)
            squared += ((source_world["x"] - destination_world["x"]) ** 2 +
                        (source_world["y"] - destination_world["y"]) ** 2)
        edge_rmse = math.sqrt(squared / len(edge["source_points"]))
        total_squared += squared
        total_count += len(edge["source_points"])
        edge_results.append({
            "stream_ids": [edge["source_stream_id"], edge["destination_stream_id"]],
            "common_marker_ids": edge["common_ids"],
            "pair_rmse_mm": edge["pair_rmse_mm"],
            "global_rmse_mm": edge_rmse,
        })

    # 모든 스트림에서 같은 ID가 어떻게 포개졌는지 검증 화면에 표시한다.
    marker_world_points = {}
    for stream_id in streams:
        local_h = stream_results[stream_id]["H_pixel_to_world"]
        for marker_id, corners in stream_corners[stream_id].items():
            world_corners = [transform_point(transforms[stream_id],
                                              transform_point(local_h, point))
                             for point in corners]
            center = {
                "x": sum(point["x"] for point in world_corners) / 4.0,
                "y": sum(point["y"] for point in world_corners) / 4.0,
            }
            marker_world_points.setdefault(marker_id, []).append((stream_id, center))
    verification_markers = []
    for marker_id, values in sorted(marker_world_points.items()):
        if len(values) < 2:
            continue
        verification_markers.append({
            "id": marker_id,
            "x": sum(value[1]["x"] for value in values) / len(values),
            "y": sum(value[1]["y"] for value in values) / len(values),
            "stream_count": len(values),
        })
    return transforms, edge_results, skipped_pairs, (
        math.sqrt(total_squared / total_count) if total_count else 0.0), verification_markers


def camera_urls(channel_id=1, profile_override=None, camera_entry=None):
    """공통 camera_list.json의 특정 CCTV·채널로 URL을 선택함."""
    entry = camera_entry or CAMERA.get("camera", {})
    configured_connection = entry.get("connection", {})
    connection = dict(configured_connection) if isinstance(configured_connection, dict) else {}
    camera = dict(entry)
    camera.update(connection)
    username = quote(str(connection.get("username", "")), safe="")
    password = quote(str(connection.get("password", "")), safe="")
    ip = str(connection.get("ip", ""))
    rtsp_port = int(connection.get("rtsp_port", 554))
    http_port = int(connection.get("http_port", 80))
    channel_id = int(channel_id)
    model_channels = load_camera_model_channels()
    channel_limit = model_channels.get(str(entry.get("model", "")), MAX_SUPPORTED_CHANNELS)
    if channel_id not in range(1, channel_limit + 1):
        raise ValueError(f"camera.channel_id must be between 1 and {channel_limit}")
    channel_list = entry.get("channels", [])
    channel_config = next((item for item in channel_list
                           if isinstance(item, dict) and int(item.get("channel", -1)) == channel_id), None)
    if channel_config is None:
        raise ValueError(f"{CAMERA_LIST_CONFIG}에 {entry.get('camera_id')} 채널 {channel_id} 설정이 없습니다")
    rtsp_url = str(channel_config.get("rtsp_url", "")).strip()
    if not rtsp_url.startswith("rtsp://"):
        profile = str(profile_override or connection.get("profile", "profile2")).strip("/")
        auth = f"{username}:{password}@" if username else ""
        rtsp_url = f"rtsp://{auth}{ip}:{rtsp_port}/{channel_id - 1}/onvif/{profile}/media.smp"
    capture_profile = str(connection.get("capture_profile", "1")).strip("/")
    if capture_profile.lower().startswith("profile"):
        capture_profile = capture_profile[7:]
    capture_profile = quote(capture_profile, safe="")
    snapshot_url = (
        f"http://{ip}:{http_port}/stw-cgi/video.cgi?msubmenu=snapshot&action=view"
        f"&Profile={capture_profile}&Channel={channel_id - 1}"
    )
    return connection, camera, rtsp_url, snapshot_url


def capture_high_resolution_frame(channel_id, timeout, camera_entry=None):
    """캡처 전용 HTTP 스냅샷에서 원본 JPEG 한 장을 가져옴."""
    connection, camera, _, snapshot_url = camera_urls(channel_id, camera_entry=camera_entry)
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
    """한 번에 선택한 스트림의 연결을 유지하고 최신 JPEG를 보관함."""

    def __init__(self):
        self.condition = threading.Condition()
        self.frames = {}
        self.sequences = {}
        self.active_stream_id = None
        self.worker = None
        self.stop_event = None
        self.process = None

    def ensure_worker(self, stream_id, camera_entry=None):
        stream_id = str(stream_id)
        channel_id = int(camera_entry["channel"]) if camera_entry else int(stream_id.rsplit("_CH_", 1)[-1])
        with self.condition:
            if self.active_stream_id == stream_id and self.worker and self.worker.is_alive():
                return
            if self.stop_event:
                self.stop_event.set()
            if self.process:
                self.process.kill()
            self.active_stream_id = stream_id
            self.frames.pop(stream_id, None)
            self.sequences.pop(stream_id, None)
            self.stop_event = threading.Event()
            self.worker = threading.Thread(target=self._receive,
                                           args=(stream_id, camera_entry, channel_id, self.stop_event), daemon=True)
            self.worker.start()
            self.condition.notify_all()

    def stop(self, channel_id=None):
        with self.condition:
            if channel_id is not None and self.active_stream_id != channel_id:
                return
            if self.stop_event:
                self.stop_event.set()
            if self.process:
                self.process.kill()
            self.active_stream_id = None
            self.frames.clear()
            self.sequences.clear()
            self.condition.notify_all()

    def _receive(self, stream_id, camera_entry, channel_id, stop_event):
        connection, camera, rtsp_url, snapshot_url = camera_urls(
            channel_id, camera_entry=camera_entry["camera"] if camera_entry else None)
        source = str(connection.get("source_type", "rtsp")).lower()
        timeout = float(connection.get("timeout_sec", 10))
        if source == "snapshot":
            self._receive_snapshots(stream_id, camera, snapshot_url, timeout, stop_event)
            return
        command = ["ffmpeg", "-nostdin", "-loglevel", "error", "-rtsp_transport", "tcp",
                   "-i", rtsp_url, "-f", "image2pipe", "-vcodec", "mjpeg", "-q:v", "5", "pipe:1"]
        while not stop_event.is_set():
            process = None
            try:
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                with self.condition:
                    self.process = process
                self._read_mjpeg(stream_id, process, stop_event)
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

    def _receive_snapshots(self, stream_id, camera, url, timeout, stop_event):
        manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        manager.add_password(None, url, camera.get("username", ""), camera.get("password", ""))
        opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))
        while not stop_event.is_set():
            try:
                with opener.open(url, timeout=timeout) as response:
                    self._set_frame(stream_id, response.read())
            except (OSError, urllib.error.URLError):
                pass
            time.sleep(CAMERA_RETRY_DELAY_SEC)

    def _read_mjpeg(self, stream_id, process, stop_event):
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
                self._set_frame(stream_id, buffer[start:end + 2])
                buffer = buffer[end + 2:]

    def _set_frame(self, stream_id, frame):
        if not frame:
            return
        with self.condition:
            if self.active_stream_id != stream_id:
                return
            self.frames[stream_id] = frame
            self.sequences[stream_id] = self.sequences.get(stream_id, 0) + 1
            self.condition.notify_all()

    def latest_packet(self, stream_id, camera_entry=None, timeout=10, previous_sequence=None,
                      stop_event=None, activate=True):
        stream_id = str(stream_id)
        if stop_event and stop_event.is_set():
            raise RuntimeError("camera channel worker stopped")
        if activate:
            self.ensure_worker(stream_id, camera_entry)
        deadline = time.monotonic() + timeout
        with self.condition:
            while (self.active_stream_id != stream_id or stream_id not in self.frames or
                   self.sequences[stream_id] == previous_sequence):
                if stop_event and stop_event.is_set():
                    raise RuntimeError("camera channel worker stopped")
                if self.active_stream_id != stream_id:
                    raise RuntimeError("camera stream switched")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("camera frame unavailable")
                self.condition.wait(remaining)
            return self.sequences[stream_id], self.frames[stream_id]

    def latest(self, stream_id, camera_entry=None, timeout=10):
        return self.latest_packet(stream_id, camera_entry, timeout)[1]


CAMERA_STREAM = CameraStream()


class CameraDetectionStream:
    """수신 중인 최신 프레임을 C++ 엔진으로 검출하고 오버레이를 갱신함."""

    def __init__(self, source):
        self.source = source
        self.condition = threading.Condition()
        self.frames = {}
        self.sequences = {}
        self.results = {}
        self.active_stream_id = None
        self.worker = None
        self.stop_event = None

    def ensure_worker(self, stream_id, camera_entry=None):
        stream_id = str(stream_id)
        with self.condition:
            if self.active_stream_id == stream_id and self.worker and self.worker.is_alive():
                return
            if self.stop_event:
                self.stop_event.set()
            self.active_stream_id = stream_id
            self.frames.pop(stream_id, None)
            self.sequences.pop(stream_id, None)
            self.results.pop(stream_id, None)
            self.stop_event = threading.Event()
            self.worker = threading.Thread(target=self._detect,
                                           args=(stream_id, camera_entry, self.stop_event), daemon=True)
            self.worker.start()
            self.condition.notify_all()

    def stop(self, channel_id=None):
        with self.condition:
            if channel_id is not None and self.active_stream_id != channel_id:
                return
            if self.stop_event:
                self.stop_event.set()
            self.active_stream_id = None
            self.frames.clear()
            self.sequences.clear()
            self.results.clear()
            self.condition.notify_all()

    def _detect(self, stream_id, camera_entry, stop_event):
        previous_sequence = None
        safe_name = stream_id.replace("/", "_")
        input_path = LIVE_CAMERA_ROOT / f"{safe_name}.jpg"
        output_path = LIVE_CAMERA_ROOT / f"{safe_name}-overlay.jpg"
        metadata_path = LIVE_CAMERA_ROOT / f"{safe_name}.json"
        self.source.ensure_worker(stream_id, camera_entry)
        while not stop_event.is_set():
            try:
                previous_sequence, frame = self.source.latest_packet(
                    stream_id, camera_entry=camera_entry, previous_sequence=previous_sequence,
                    stop_event=stop_event,
                    activate=False)
                input_path.write_bytes(frame)
                result = run_tool(["detect-markers", "--config", str(CONFIG),
                                   "--input", str(input_path), "--output", str(metadata_path),
                                   "--overlay", str(output_path)])
                if result["ok"] and output_path.is_file():
                    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
                    self._set_result(stream_id, metadata, output_path.read_bytes())
                elif not result["ok"]:
                    time.sleep(CAMERA_RETRY_DELAY_SEC)
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
                time.sleep(CAMERA_RETRY_DELAY_SEC)

    def _set_result(self, stream_id, metadata, frame):
        with self.condition:
            if self.active_stream_id != stream_id:
                return
            self.results[stream_id] = metadata
            self.frames[stream_id] = frame
            self.sequences[stream_id] = self.sequences.get(stream_id, 0) + 1
            self.condition.notify_all()

    def current_result(self, stream_id, camera_entry=None):
        self.ensure_worker(stream_id, camera_entry)
        with self.condition:
            return self.results.get(stream_id, {"ids": [], "corners": [], "image_size": {}})

    def latest_packet(self, stream_id, timeout=10, previous_sequence=None, activate=True):
        stream_id = str(stream_id)
        if activate:
            self.ensure_worker(stream_id)
        deadline = time.monotonic() + timeout
        with self.condition:
            while (self.active_stream_id != stream_id or stream_id not in self.frames or
                   self.sequences[stream_id] == previous_sequence):
                if self.active_stream_id != stream_id:
                    raise RuntimeError("detected camera stream switched")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("detected camera frame unavailable")
                self.condition.wait(remaining)
            return self.sequences[stream_id], self.frames[stream_id]

    def latest(self, stream_id, timeout=10):
        return self.latest_packet(stream_id, timeout)[1]


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


def requested_stream(value):
    """HTTP 요청의 전역 stream_id를 우선 해석하고 구버전 channel 요청도 임시 호환한다."""
    if isinstance(value, dict):
        stream_id = value.get("stream_id")
        if stream_id:
            return configured_stream(stream_id=str(stream_id))
        return configured_stream(channel=value.get("channel"))
    return configured_stream(stream_id=value)


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
                            "homography_tool": TOOL, "port": PORT,
                            "camera_id": configured_camera_id(),
                            "camera_model": CAMERA_MODEL,
                            "channel_count": MAX_SUPPORTED_CHANNELS,
                            "camera_list": camera_status_entries(),
                            "streams": stream_status_entries(),
                            "camera_models": CAMERA_MODELS.get("models", []),
                            "max_channels": MAX_SUPPORTED_CHANNELS,
                            "min_common_markers": MIN_COMMON_MARKERS,
                            "max_verification_streams": MAX_VERIFICATION_STREAMS,
                            "global_alignment": True})
            return
        if path == "/api/camera/frame":
            try:
                query = urlparse(self.path).query
                stream = requested_stream(parse_qs(query).get("stream_id", [None])[0]
                                          or {"channel": parse_qs(query).get("channel", [1])[0]})
                body = CAMERA_STREAM.latest(stream["stream_id"], stream)
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
                stream_entry = requested_stream(params.get("stream_id", [None])[0]
                                                or {"channel": params.get("channel", [1])[0]})
                stream_id = stream_entry["stream_id"]
                stream = CAMERA_DETECTION_STREAM if params.get("overlay", ["0"])[0] == "1" else CAMERA_STREAM
                camera_urls(stream_entry["channel"], camera_entry=stream_entry["camera"])
                stream.ensure_worker(stream_id, stream_entry)
                sequence, frame = stream.latest_packet(stream_id, activate=False)
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
                    sequence, frame = stream.latest_packet(stream_id, previous_sequence=sequence,
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
                params = parse_qs(query)
                stream_entry = requested_stream(params.get("stream_id", [None])[0]
                                                or {"channel": params.get("channel", [1])[0]})
                camera_urls(stream_entry["channel"], camera_entry=stream_entry["camera"])
                self.send_json(CAMERA_DETECTION_STREAM.current_result(
                    stream_entry["stream_id"], stream_entry))
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

        if path == "/api/camera/settings":
            try:
                saved = save_camera_settings(payload.get("camera_id"), payload.get("camera_model"))
                self.send_json({"ok": True, **saved,
                                "camera_list": camera_status_entries(),
                                "streams": stream_status_entries(),
                                "camera_models": CAMERA_MODELS.get("models", [])})
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self.send_json({"ok": False, "error": str(error)}, 400)
            return

        if path == "/api/camera/detect":
            cleanup_results()
            job_id = uuid.uuid4().hex
            job_dir = RESULT_ROOT / job_id
            job_dir.mkdir(parents=True)
            try:
                stream_entry = requested_stream(payload)
                stream_id = stream_entry["stream_id"]
                channel_id = stream_entry["channel"]
                camera_entry = stream_entry["camera"]
                image_path = job_dir / "capture.jpg"
                CAMERA_STREAM.ensure_worker(stream_id, stream_entry)
                preview_connection, _, _, _ = camera_urls(
                    channel_id, camera_entry=camera_entry)
                capture_timeout = float(preview_connection.get("timeout_sec", 10))
                _, preview_frame = CAMERA_STREAM.latest_packet(stream_id, camera_entry=stream_entry,
                                                                timeout=capture_timeout,
                                                                activate=False)
                preview_path = job_dir / "rtsp-capture.jpg"
                preview_path.write_bytes(preview_frame)
                frame = capture_high_resolution_frame(channel_id, capture_timeout, camera_entry)
                image_path.write_bytes(frame)
                CAMERA_DETECTION_STREAM.stop(stream_id)
                CAMERA_STREAM.stop(stream_id)
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
                detected["stream_id"] = stream_id
                detected["camera_id"] = stream_entry["camera_id"]
                detected["channel"] = channel_id
                detected["rtsp_detection"] = rtsp_detected
                detected["rtsp_image_url"] = f"/artifacts/{job_id}/rtsp-capture.jpg"
                detected["rtsp_alignment"] = alignment
                detected["rtsp_alignment_error"] = alignment_error
                (job_dir / "capture-meta.json").write_text(json.dumps({
                    "capture_id": job_id, "stream_id": stream_id,
                    "camera_id": stream_entry["camera_id"], "channel": channel_id,
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
                          "corner_overrides": payload.get("corner_overrides", {})}
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
                capture_meta = json.loads((job_dir / "capture-meta.json").read_text(
                    encoding="utf-8")) if (job_dir / "capture-meta.json").is_file() else {}
                value["stream_id"] = capture_meta.get("stream_id")
                value["camera_id"] = capture_meta.get("camera_id")
                value["channel"] = capture_meta.get("channel")
                value.setdefault("verification_region_world", None)
                output_file.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                                       encoding="utf-8")
                # 로컬 H 산출 단계에서는 운영 파일을 만들지 않는다.
                # 전체 스트림 정합이 끝난 최종 H만 /api/homography/global-align에서 저장한다.
                storage = None
                # 저장 함수에서 최소 운영 파일의 필수 필드를 이미 검증했다.
                if value.get("world_unit") != "mm":
                    raise ValueError("호모그래피 world_unit은 mm여야 합니다")
                self.send_json({"ok": True, "result": value,
                    "artifact_url": f"/artifacts/{capture_id}/{output_name}",
                    "overlay_url": f"/artifacts/{capture_id}/{overlay_name}",
                    "operational_path": storage["path"] if storage else None,
                    "storage": storage})
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self.send_json({"ok": False, "error": str(error)}, 400)
            return
        if path == "/api/homography/global-align":
            try:
                # 캡처·로컬 H가 있는 모든 CCTV×채널을 한 번에 전체 맵에 올린다.
                stream_ids = sorted(set(str(value) for value in payload.get("stream_ids", [])))
                if len(stream_ids) < 2:
                    raise ValueError("전체 스트림 정합에는 최소 두 스트림이 필요합니다")
                configured = {entry["stream_id"]: entry for entry in configured_stream_entries()}
                unknown = [stream_id for stream_id in stream_ids if stream_id not in configured]
                if unknown:
                    raise ValueError("등록되지 않은 stream_id: " + ", ".join(unknown))
                anchor_stream_id = str(payload.get("anchor_stream_id", ""))
                if anchor_stream_id not in stream_ids:
                    raise ValueError("anchor_stream_id는 전체 정합 스트림 중 하나여야 합니다")
                capture_ids = payload.get("capture_ids", {})
                if not isinstance(capture_ids, dict):
                    raise ValueError("capture_ids가 필요합니다")

                stream_results = {}
                stream_corners = {}
                for stream_id in stream_ids:
                    capture_id = capture_ids.get(stream_id)
                    job_dir = capture_directory(capture_id)
                    output_name = configured_output_name("manual", "homography_manual.json")
                    if Path(output_name).suffix.lower() != ".json":
                        output_name += ".json"
                    result_file = job_dir / output_name
                    if not result_file.is_file():
                        raise ValueError(f"{stream_id}의 로컬 호모그래피가 없습니다")
                    result = json.loads(result_file.read_text(encoding="utf-8"))
                    if str(result.get("stream_id", "")) != stream_id:
                        raise ValueError(f"{stream_id} 캡처와 보정 결과의 stream_id가 다릅니다")
                    if result.get("camera_id") != configured[stream_id]["camera_id"] or \
                            int(result.get("channel", -1)) != configured[stream_id]["channel"]:
                        raise ValueError(f"{stream_id} 캡처와 camera_list 채널 정보가 다릅니다")
                    stream_results[stream_id] = result
                    stream_corners[stream_id] = capture_marker_corners(job_dir, result)

                transforms, edge_results, skipped_pairs, global_rmse, verification_markers = \
                    align_all_streams(stream_results, stream_corners, anchor_stream_id)
                global_h = {
                    channel: matrix_multiply(
                        transforms[channel], stream_results[channel]["H_pixel_to_world"])
                    for channel in stream_ids
                }

                storage = {}
                for stream_id in stream_ids:
                    entry = configured[stream_id]
                    final_value = {
                        "schema_version": 2,
                        "world_unit": "mm",
                        "camera_id": entry["camera_id"],
                        "stream_id": stream_id,
                        "channel": entry["channel"],
                        "H_pixel_to_world": global_h[stream_id],
                        "image_size": stream_results[stream_id]["image_size"],
                    }
                    storage[stream_id] = save_operational_homography(final_value)

                verification_bounds = union_world_bounds([
                    image_world_bounds(global_h[stream_id], stream_results[stream_id]["image_size"])
                    for stream_id in stream_ids
                ])
                verification_channels = {}
                for stream_id in stream_ids:
                    capture_id = capture_ids.get(stream_id)
                    entry = configured[stream_id]
                    verification_channels[stream_id] = {
                        "stream_id": stream_id,
                        "camera_id": entry["camera_id"],
                        "channel": entry["channel"],
                        "capture_id": capture_id,
                        "image_url": f"/artifacts/{capture_id}/capture.jpg",
                        "image_size": stream_results[stream_id]["image_size"],
                        "H_pixel_to_world": global_h[stream_id],
                    }
                self.send_json({
                    "ok": True,
                    "anchor_stream_id": anchor_stream_id,
                    "stream_ids": stream_ids,
                    "edge_results": edge_results,
                    "skipped_pairs": skipped_pairs,
                    "global_rmse_mm": global_rmse,
                    "storage": storage,
                    "local_to_global": {
                        stream_id: transforms[stream_id] for stream_id in stream_ids
                    },
                    "verification": {
                        "coordinate_frame": "BOARD_GLOBAL_MM",
                        "bounds_mm": verification_bounds,
                        "streams": verification_channels,
                        "common_markers": verification_markers,
                    },
                })
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
