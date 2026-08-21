#!/usr/bin/env python3
"""Publish fake sensor samples to the server's MQTT uplink.

The sender deliberately uses the same topology as the production path:
``forklift/sensor/<terminal_id>`` identifies the terminal and the JSON payload
contains only that terminal's sensor sample. A single MQTT connection can
publish samples for any number of terminals.

This script uses only Python's standard library, so it can run on the target
Raspberry Pi without installing paho-mqtt or relying on mosquitto_pub.
"""

import argparse
import json
import os
import random
import socket
import sys
import time


DEFAULT_BROKER_HOST = "127.0.0.1"
DEFAULT_BROKER_PORT = 1883
DEFAULT_KEEPALIVE_SECONDS = 60


def encode_remaining_length(length):
    encoded = bytearray()
    while True:
        digit = length % 128
        length //= 128
        if length:
            digit |= 0x80
        encoded.append(digit)
        if not length:
            return bytes(encoded)


def encode_utf8(value):
    encoded = value.encode("utf-8")
    if len(encoded) > 0xFFFF:
        raise ValueError("MQTT UTF-8 field is too long")
    return len(encoded).to_bytes(2, "big") + encoded


def recv_exact(sock, size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("MQTT broker closed the connection")
        chunks.extend(chunk)
    return bytes(chunks)


def read_packet(sock):
    header = recv_exact(sock, 1)[0]
    multiplier = 1
    remaining_length = 0
    for _ in range(4):
        digit = recv_exact(sock, 1)[0]
        remaining_length += (digit & 0x7F) * multiplier
        if not digit & 0x80:
            break
        multiplier *= 128
    else:
        raise ValueError("invalid MQTT remaining length")
    return header >> 4, recv_exact(sock, remaining_length)


def connect_mqtt(host, port, client_id):
    sock = socket.create_connection((host, port), timeout=5)
    connect_payload = encode_utf8(client_id)
    variable_header = encode_utf8("MQTT") + bytes((4, 0x02)) + DEFAULT_KEEPALIVE_SECONDS.to_bytes(2, "big")
    packet = variable_header + connect_payload
    sock.sendall(bytes((0x10,)) + encode_remaining_length(len(packet)) + packet)

    packet_type, connack = read_packet(sock)
    if packet_type != 2 or len(connack) < 2 or connack[1] != 0:
        return_code = connack[1] if len(connack) >= 2 else "unknown"
        raise ConnectionError("MQTT CONNECT rejected (return code: %s)" % return_code)
    return sock


def publish_qos0(sock, topic, payload):
    packet = encode_utf8(topic) + payload
    sock.sendall(bytes((0x30,)) + encode_remaining_length(len(packet)) + packet)


def ping(sock):
    sock.sendall(b"\xC0\x00")
    packet_type, _ = read_packet(sock)
    if packet_type != 13:
        raise ConnectionError("unexpected MQTT packet while waiting for PINGRESP")


def disconnect(sock):
    try:
        sock.sendall(b"\xE0\x00")
    finally:
        sock.close()


def parse_args():
    parser = argparse.ArgumentParser(description="MQTT 센서 업링크 가짜 송신기")
    parser.add_argument("--host", default=DEFAULT_BROKER_HOST, help="MQTT 브로커 주소")
    parser.add_argument("--port", type=int, default=DEFAULT_BROKER_PORT, help="MQTT 브로커 포트")
    parser.add_argument(
        "--terminal-id",
        action="append",
        required=True,
        metavar="ID",
        help="센서를 보낼 단말 ID. 여러 단말은 이 옵션을 반복 지정",
    )
    parser.add_argument("--camera-id", default="CAM_01", help="센서 payload의 camera_id")
    parser.add_argument("--interval", type=float, default=0.5, help="단말별 전송 주기(초)")
    parser.add_argument("--distance", type=int, default=800, help="초기 ToF 거리(mm)")
    parser.add_argument(
        "--disconnect-after",
        type=float,
        default=None,
        metavar="SECONDS",
        help="지정한 시간이 지나면 MQTT 연결을 닫고 종료 (stale 테스트용)",
    )
    parser.add_argument(
        "--corrupt-every",
        type=int,
        default=0,
        metavar="N",
        help="N번째 발행마다 깨진 JSON을 전송 (0=사용 안 함)",
    )
    return parser.parse_args()


def make_sample(camera_id, tof_mm, producer_run_id, sequence):
    return {
        "schema_version": 1,
        "message_id": "%s-m%d" % (producer_run_id, sequence),
        "producer_run_id": producer_run_id,
        "sequence": sequence,
        "camera_id": camera_id,
        "tof_ok": True,
        "tof_distance_mm": tof_mm,
        "imu_ok": True,
        "imu_accel_x_g": round(random.uniform(-0.03, 0.03), 3),
        "imu_accel_y_g": round(random.uniform(-0.03, 0.03), 3),
        "imu_accel_z_g": round(1.00 + random.uniform(-0.02, 0.02), 3),
        "ts_ms": int(time.time() * 1000),
    }


def next_distance(tof_mm):
    return max(200, min(3000, tof_mm + random.randint(-60, 60)))


def run(args):
    if args.interval <= 0:
        raise ValueError("--interval must be greater than zero")
    if args.distance < 0:
        raise ValueError("--distance must not be negative")
    if args.corrupt_every < 0:
        raise ValueError("--corrupt-every must not be negative")

    client_id = "fake-sensor-" + str(int(time.time() * 1000))
    sock = connect_mqtt(args.host, args.port, client_id)
    print("[송신기] MQTT 연결 완료 - %s:%d" % (args.host, args.port))
    print("[송신기] 대상 단말 - %s" % ", ".join(args.terminal_id))

    distances = {terminal_id: args.distance for terminal_id in args.terminal_id}
    started = time.monotonic()
    last_io = started
    sent = 0
    producer_run_id = "%s-p%d" % (time.strftime("%Y%m%dT%H%M%S", time.gmtime()), os.getpid())
    try:
        while True:
            if args.disconnect_after is not None and time.monotonic() - started >= args.disconnect_after:
                print("[송신기] --disconnect-after %.1fs 도달 - MQTT 연결 종료" % args.disconnect_after)
                return

            for terminal_id in args.terminal_id:
                sent += 1
                topic = "forklift/sensor/" + terminal_id
                if args.corrupt_every and sent % args.corrupt_every == 0:
                    payload = b'{"camera_id":'
                else:
                    distances[terminal_id] = next_distance(distances[terminal_id])
                    payload = json.dumps(
                        make_sample(args.camera_id, distances[terminal_id], producer_run_id, sent),
                        separators=(",", ":"),
                    ).encode("utf-8")

                publish_qos0(sock, topic, payload)
                last_io = time.monotonic()
                print("[송신기] %s <- %s" % (topic, payload.decode("utf-8")))

            deadline = time.monotonic() + args.interval
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                if time.monotonic() - last_io >= DEFAULT_KEEPALIVE_SECONDS / 2:
                    ping(sock)
                    last_io = time.monotonic()
                time.sleep(min(remaining, 0.25))
    finally:
        disconnect(sock)
        print("[송신기] MQTT 연결 정리 완료")


def main():
    args = parse_args()
    try:
        run(args)
    except KeyboardInterrupt:
        print("\n[송신기] Ctrl+C - 종료합니다")
    except (ConnectionError, OSError, ValueError) as error:
        print("[오류] %s" % error, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
