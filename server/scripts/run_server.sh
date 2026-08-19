#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -d "${SCRIPT_DIR}/server" ]; then
    SERVER_ROOT="${SCRIPT_DIR}/server"
else
    SERVER_ROOT="${SCRIPT_DIR}"
fi

# 1. Mosquitto 브로커 활성화 확인
if ! systemctl is-active --quiet mosquitto 2>/dev/null; then
    echo "[알림] Mosquitto 브로커를 시작합니다..."
    sudo -n systemctl start mosquitto 2>/dev/null || true
fi

# 2. 백그라운드 인스턴스 충돌 방지 (중복 접속 핑퐁 방어)
pkill -f "/usr/local/bin/forklift_safety_server" 2>/dev/null || true
sudo -n systemctl stop forklift_safety_server 2>/dev/null || true

# 3. 바이너리 존재 확인 및 자동 빌드
BINARY="${SERVER_ROOT}/build/apps/main_app/forklift_safety_server"
if [ ! -f "${BINARY}" ]; then
    echo "[알림] 안전 서버를 빌드합니다..."
    mkdir -p "${SERVER_ROOT}/build"
    cd "${SERVER_ROOT}/build"
    cmake ..
    make -j"$(nproc)"
fi

# 4. 로컬 안전 서버 실행
echo "[기동] 로컬 안전 서버를 실행합니다..."
cd "${SERVER_ROOT}"
exec "${BINARY}" "$@"
