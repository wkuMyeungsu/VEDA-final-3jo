#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

takeover=false
server_args=()
for argument in "$@"; do
    if [ "${argument}" = "--takeover" ]; then
        takeover=true
    else
        server_args+=("${argument}")
    fi
done

# 1. Mosquitto 브로커 활성화 확인
if ! systemctl is-active --quiet mosquitto 2>/dev/null; then
    sudo -n systemctl start mosquitto 2>/dev/null || true
fi

# 2. 운영 인스턴스 보호. 로컬 실행이 운영 서비스를 암묵적으로 중지하면 안 된다.
service_active=false
deployed_process_active=false
if systemctl is-active --quiet forklift_safety_server 2>/dev/null; then
    service_active=true
fi
if pgrep -f '^/usr/local/bin/forklift_safety_server( |$)' >/dev/null 2>&1; then
    deployed_process_active=true
fi

if ${service_active} || ${deployed_process_active}; then
    if ! ${takeover}; then
        echo "운영 안전 서버가 실행 중입니다. 로컬 실행으로 교체하려면 --takeover를 명시하세요." >&2
        exit 3
    fi
    sudo -n systemctl stop forklift_safety_server
    pkill -f '^/usr/local/bin/forklift_safety_server( |$)' 2>/dev/null || true
fi

# 3. 바이너리 존재 확인 및 자동 빌드
BINARY="${SERVER_ROOT}/build/apps/main_app/forklift_safety_server"
if [ ! -f "${BINARY}" ]; then
    mkdir -p "${SERVER_ROOT}/build"
    cd "${SERVER_ROOT}/build"
    cmake ..
    make -j"$(nproc)"
fi

# 4. 로컬 안전 서버 실행
cd "${SERVER_ROOT}"
exec "${BINARY}" "${server_args[@]}"
