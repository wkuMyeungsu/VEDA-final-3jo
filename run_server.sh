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
    sudo systemctl start mosquitto || true
fi

# 2. 바이너리 존재 확인 및 자동 빌드
BINARY="${SERVER_ROOT}/build/apps/main_app/forklift_safety_server"
if [ ! -f "${BINARY}" ]; then
    echo "[알림] 안전 서버를 빌드합니다..."
    mkdir -p "${SERVER_ROOT}/build"
    cd "${SERVER_ROOT}/build"
    cmake ..
    make -j"$(nproc)"
fi

# 3. 로컬 안전 서버 실행
cd "${SERVER_ROOT}"
exec "${BINARY}" "$@"
