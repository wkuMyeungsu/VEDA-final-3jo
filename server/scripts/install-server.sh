#!/usr/bin/env bash
set -euo pipefail

# Raspberry Pi 운영 설치 스크립트.
# 실행: sudo ./server/scripts/install-server.sh
#
# 개인키는 저장소에 넣지 않는다. 기본 인증서 원본은 별도 MQTT 작업 디렉터리이며,
# 필요하면 MQTT_CERT_SOURCE 환경변수로 바꿀 수 있다.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MQTT_CERT_SOURCE="${MQTT_CERT_SOURCE:-/home/veda3/forklift-safety-mqtt/central-server}"

reset_stale_cmake_build() {
    local build_dir="$1"
    local expected_source="$2"
    local cache_file="${build_dir}/CMakeCache.txt"

    if [ ! -f "${cache_file}" ]; then
        return
    fi

    local cached_source
    cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache_file}")"
    if [ "${cached_source}" != "${expected_source}" ]; then
        echo "오래된 CMake 캐시 제거: ${build_dir}"
        rm -rf -- "${build_dir}"
    fi
}

if [ "${EUID}" -ne 0 ]; then
    echo "root 권한이 필요합니다: sudo ${SCRIPT_DIR}/install-server.sh" >&2
    exit 1
fi

for required in ca.crt client-server.crt client-server.key; do
    if [ ! -f "${MQTT_CERT_SOURCE}/${required}" ]; then
        echo "인증서 파일이 없습니다: ${MQTT_CERT_SOURCE}/${required}" >&2
        exit 1
    fi
done

echo "=== [1/7] 안전 서버 빌드 ==="
reset_stale_cmake_build "${SERVER_ROOT}/build" "${SERVER_ROOT}"
cmake -S "${SERVER_ROOT}" -B "${SERVER_ROOT}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${SERVER_ROOT}/build" -j"$(nproc)"

echo "=== [2/7] 호모그래피 엔진 빌드 ==="
reset_stale_cmake_build "${SERVER_ROOT}/apps/homography_app/processing/build" \
    "${SERVER_ROOT}/apps/homography_app/processing"
cmake -S "${SERVER_ROOT}/apps/homography_app/processing" \
      -B "${SERVER_ROOT}/apps/homography_app/processing/build" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build "${SERVER_ROOT}/apps/homography_app/processing/build" -j"$(nproc)"

echo "=== [3/7] 설정·로그·인증서 디렉터리 설치 ==="
install -d -m 0750 -o veda3 -g veda3 \
    /etc/forklift_safety /etc/forklift_safety/safety /etc/forklift_safety/certs \
    /var/log/forklift_safety
cp -a "${SERVER_ROOT}/config/." /etc/forklift_safety/
chown -R root:root /etc/forklift_safety
find /etc/forklift_safety -type d -exec chmod 0755 {} +
find /etc/forklift_safety -type f -exec chmod 0644 {} +
install -o veda3 -g veda3 -m 0644 "${MQTT_CERT_SOURCE}/ca.crt" \
    /etc/forklift_safety/certs/ca.crt
install -o veda3 -g veda3 -m 0644 "${MQTT_CERT_SOURCE}/client-server.crt" \
    /etc/forklift_safety/certs/client-server.crt
install -o veda3 -g veda3 -m 0600 "${MQTT_CERT_SOURCE}/client-server.key" \
    /etc/forklift_safety/certs/client-server.key
chown -R veda3:veda3 /var/log/forklift_safety

echo "=== [4/7] 실행 파일 설치 ==="
install -o root -g root -m 0755 \
    "${SERVER_ROOT}/build/apps/main_app/forklift_safety_server" \
    /usr/local/bin/forklift_safety_server
install -o root -g root -m 0755 \
    "${SERVER_ROOT}/build/apps/main_app/event_log_viewer" \
    /usr/local/bin/event_log_viewer

echo "=== [5/7] systemd 유닛 설치 ==="
install -o root -g root -m 0644 "${SCRIPT_DIR}/forklift_safety_server.service" \
    /etc/systemd/system/forklift_safety_server.service
install -o root -g root -m 0644 "${SCRIPT_DIR}/homography-app.service" \
    /etc/systemd/system/homography-app.service
install -o root -g root -m 0644 "${SCRIPT_DIR}/monitoring-app.service" \
    /etc/systemd/system/monitoring-app.service
systemctl daemon-reload

echo "=== [6/7] 기존 운영 콘솔 교체 ==="
systemctl disable --now server-ops.service 2>/dev/null || true
systemctl disable --now homography-app.service 2>/dev/null || true
systemctl enable mosquitto.service
systemctl enable monitoring-app.service forklift_safety_server.service

echo "=== [7/7] 서비스 기동 ==="
systemctl restart mosquitto.service
systemctl restart monitoring-app.service
systemctl restart forklift_safety_server.service
systemctl --no-pager --full status mosquitto.service monitoring-app.service \
    forklift_safety_server.service
