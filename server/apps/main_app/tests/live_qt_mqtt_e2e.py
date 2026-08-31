#!/usr/bin/env python3
"""Subscribe to the live risk topic and evaluate payloads with the Qt HUD rules."""

import json
import os
import subprocess
import sys

HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
PORT = os.environ.get("MQTT_PORT", "8883")
CA = os.environ.get("MQTT_CA", "/etc/forklift_safety/certs/ca.crt")
CERT = os.environ.get("MQTT_CERT", "/home/veda3/forklift-safety-mqtt/term01/client-term01.crt")
KEY = os.environ.get("MQTT_KEY", "/home/veda3/forklift-safety-mqtt/term01/client-term01.key")
TOPIC = "forklift/risk/TERM_01"
ACTIVE = "CAM_01_CH_02"


def exception_from_string(value):
    mapping = {
        "SENSOR_FAULT": 1,
        "DEAD_RECKONING": 2,
        "EMERGENCY_IMPACT": 3,
        "NETWORK_DISCONNECTED": 4,
        "CAMERA_DISCONNECTED": 5,
        "UNCONFIRMED_PROXIMITY": 6,
    }
    return mapping.get(value or "NONE", 0)


def from_json(obj):
    risk = int(obj.get("risk_level") or 0)
    if risk not in (1, 2, 3):
        risk = 0
    dist = obj.get("distance_mm")
    return {
        "stream_id": obj.get("stream_id") or "",
        "camera_id": obj.get("camera_id") or "",
        "risk": 0 if risk not in (1, 2, 3) and risk != 0 else risk,
        "exception": exception_from_string(obj.get("exception_state")),
        "distance_valid": isinstance(dist, (int, float)),
        "raw_exception": obj.get("exception_state"),
        "raw_risk": obj.get("risk_level"),
    }


def hud_visible(meta):
    return meta["risk"] > 0 or meta["exception"] > 0


def matches_active(meta):
    if meta["stream_id"] and meta["stream_id"] == ACTIVE:
        return True
    if meta["camera_id"] == ACTIVE:
        return True
    return False


def main():
    cmd = [
        "mosquitto_sub",
        "-h", HOST,
        "-p", PORT,
        "--cafile", CA,
        "--cert", CERT,
        "--key", KEY,
        "-t", TOPIC,
        "-C", "12",
        "-W", "8",
    ]
    print("LIVE MQTT", " ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if proc.returncode not in (0, 27) and not lines:
        print("FAIL mosquitto_sub rc=%s stderr=%s" % (proc.returncode, proc.stderr.strip()))
        return 1
    if not lines:
        print("FAIL no risk payloads in 8s")
        return 1

    failures = 0
    seen_bind = False
    for index, line in enumerate(lines, 1):
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as exc:
            print("  [FAIL] payload %d JSON %s" % (index, exc))
            failures += 1
            continue
        meta = from_json(obj)
        print(
            "  payload %d risk=%s exc=%s stream=%s dist=%s hud=%s match=%s"
            % (
                index,
                meta["raw_risk"],
                meta["raw_exception"],
                meta["stream_id"],
                obj.get("distance_mm"),
                hud_visible(meta),
                matches_active(meta),
            )
        )
        if meta["raw_exception"] == "DEAD_RECKONING":
            print("  [FAIL] Qt would show 자율 항법 / UNKNOWN")
            failures += 1
        if meta["exception"] > 0 and meta["risk"] == 0:
            print("  [FAIL] exception with SAFE still opens HUD")
            failures += 1
        if matches_active(meta):
            seen_bind = True
            if meta["stream_id"] and not meta["stream_id"].startswith("CAM_"):
                print("  [FAIL] stream_id contract broken")
                failures += 1

    if not seen_bind:
        print("  [WARN] no payload matched active CAM_01_CH_02 (marker may be absent)")
    if failures:
        print("=== LIVE 실패 %d건 / %d payload ===" % (failures, len(lines)))
        return 1
    print("=== LIVE 통과 %d payload · DEAD_RECKONING 없음 · HUD 예외 깜빡임 없음 ===" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
