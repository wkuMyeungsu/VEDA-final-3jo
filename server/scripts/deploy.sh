#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== [1/5] 안전 서버 빌드 진행 ==="
mkdir -p "${SERVER_ROOT}/build"
cd "${SERVER_ROOT}/build"
cmake ..
make -j"$(nproc)"

echo "=== [2/5] 바이너리 시스템 설치 (/usr/local/bin) ==="
sudo cp "${SERVER_ROOT}/build/apps/main_app/forklift_safety_server" /usr/local/bin/
sudo cp "${SERVER_ROOT}/build/apps/main_app/event_log_viewer" /usr/local/bin/

echo "=== [3/5] 설정 파일 배포 (/etc/forklift_safety) ==="
sudo mkdir -p /etc/forklift_safety
sudo cp -r "${SERVER_ROOT}/config/"* /etc/forklift_safety/

echo "=== [4/5] 로그 디렉터리 생성 및 권한 설정 (/var/log/forklift_safety) ==="
sudo mkdir -p /var/log/forklift_safety
CURRENT_USER="${SUDO_USER:-$USER}"
sudo chown -R "${CURRENT_USER}:${CURRENT_USER}" /var/log/forklift_safety

echo "=== [5/5] systemd 서비스 등록 및 즉시 시작 ==="
sudo cp "${SERVER_ROOT}/scripts/forklift_safety_server.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now forklift_safety_server

echo "=== [배포 완료] 서버 상태 확인 ==="
sudo systemctl status forklift_safety_server --no-pager
