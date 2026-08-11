#!/usr/bin/env python3
# fake_sensor_uplink_sender.py
#
# 입력값: 접속할 서버 주소/포트, 단말 식별자, 전송 주기 (아래 옵션 참고)
# 처리과정:
#   1) SensorUplinkReceiver(기본 9002)에 TCP로 접속
#   2) hello 한 줄을 보내 terminal_id를 등록
#   3) 그 뒤로 --interval 간격마다 센서 스키마 한 줄(NDJSON)을 계속 전송
#      (tof_distance_mm은 랜덤 워크로 조금씩 변하고, IMU는 정지 상태 기준 고정값 + 미세 노이즈)
# 출력값: 보낸 줄을 콘솔에 그대로 출력 (서버 쪽 로그와 눈으로 대조하기 위함)
#
# 실제 단말 송신기가 아니라 서버 수신기 검증용 스텁이다. 실제 단말 송신 코드는
# forklift-device 쪽에 별도로 들어간다 (Confluence "단말→서버 센서 업링크 채널 협의" 5-1 참고).
#
# 사용법:
#   python3 fake_sensor_uplink_sender.py                          # 127.0.0.1:9002 로 접속
#   python3 fake_sensor_uplink_sender.py --port 19002             # 테스트 포트로 접속
#   python3 fake_sensor_uplink_sender.py --disconnect-after 3     # 3초 뒤 강제 종료 (isStale 검증용)
#   python3 fake_sensor_uplink_sender.py --corrupt-every 5        # 5줄마다 깨진 JSON 섞어 보내기
#
# Ctrl+C로 종료하면 소켓을 정리하고 끝난다.

import argparse
import json
import random
import socket
import sys
import time

DEFAULT_PORT = 9002  # Confluence 제안값. 서버 SensorUplinkReceiver::kDefaultPort와 같은 값.


def parse_args():
    p = argparse.ArgumentParser(description="센서 업링크(단말→서버) 가짜 송신기")
    p.add_argument("--host", default="127.0.0.1", help="서버 주소 (기본: 127.0.0.1)")
    p.add_argument("--port", type=int, default=DEFAULT_PORT,
                   help="서버 포트 (기본: %d)" % DEFAULT_PORT)
    p.add_argument("--terminal-id", default="TERM_01", help="hello로 보낼 단말 식별자")
    p.add_argument("--camera-id", default="cam0", help="센서 줄에 실을 camera_id")
    p.add_argument("--interval", type=float, default=0.5, help="전송 주기(초, 기본: 0.5)")
    p.add_argument("--disconnect-after", type=float, default=None, metavar="N",
                   help="N초 뒤 소켓을 강제로 끊고 종료 (서버 isStale 전환 확인용)")
    p.add_argument("--corrupt-every", type=int, default=0, metavar="N",
                   help="N줄마다 깨진 JSON을 한 줄 섞는다 (0=사용 안 함)")
    return p.parse_args()


def make_sample(camera_id, tof_mm):
    # 스키마는 Confluence "단말→서버 센서 업링크 채널 협의" 3번 초안 그대로다.
    # ToF는 정수 mm, IMU는 3축 원시 g값 (magnitude 계산은 서버 담당).
    return {
        "camera_id": camera_id,
        "tof_ok": True,
        "tof_distance_mm": tof_mm,
        "imu_ok": True,
        "imu_accel_x_g": round(random.uniform(-0.03, 0.03), 3),
        "imu_accel_y_g": round(random.uniform(-0.03, 0.03), 3),
        "imu_accel_z_g": round(1.00 + random.uniform(-0.02, 0.02), 3),  # 정지 상태 중력 약 1.0g
        "ts_ms": int(time.time() * 1000),
    }


def next_distance(tof_mm):
    # 랜덤 워크. 200~3000mm 안에서만 움직이게 잘라 ToF 측정 범위를 벗어나지 않게 한다.
    tof_mm += random.randint(-60, 60)
    return max(200, min(3000, tof_mm))


def run(args):
    sock = socket.create_connection((args.host, args.port), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    print("[송신기] 접속 완료 - %s:%d" % (args.host, args.port))

    started = time.monotonic()
    try:
        hello = {"type": "hello", "terminal_id": args.terminal_id}
        sock.sendall((json.dumps(hello) + "\n").encode("utf-8"))
        print("[송신기] hello 전송 - %s" % json.dumps(hello, ensure_ascii=False))

        tof_mm = 800
        sent = 0
        while True:
            if args.disconnect_after is not None:
                if time.monotonic() - started >= args.disconnect_after:
                    print("[송신기] --disconnect-after %.1fs 도달 - 접속 종료"
                          % args.disconnect_after)
                    return

            sent += 1
            if args.corrupt_every and sent % args.corrupt_every == 0:
                line = '{"camera_id": "%s", "tof_ok": tr' % args.camera_id  # 일부러 깨뜨림
            else:
                tof_mm = next_distance(tof_mm)
                line = json.dumps(make_sample(args.camera_id, tof_mm))

            sock.sendall((line + "\n").encode("utf-8"))
            print("[송신기] %s" % line)
            time.sleep(args.interval)
    finally:
        # Ctrl+C / --disconnect-after / 예외 어느 경로로 나가든 소켓은 항상 정리한다.
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        sock.close()
        print("[송신기] 소켓 정리 완료")


def main():
    args = parse_args()
    try:
        run(args)
    except KeyboardInterrupt:
        print("\n[송신기] Ctrl+C - 종료합니다")
    except ConnectionRefusedError:
        print("[오류] 접속 거부 - 서버가 %s:%d 에서 listen 중인지 확인하세요"
              % (args.host, args.port))
        sys.exit(1)
    except OSError as e:
        print("[오류] 소켓 오류: %s" % e)
        sys.exit(1)


if __name__ == "__main__":
    main()
