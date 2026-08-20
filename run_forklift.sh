#!/bin/bash
# run_forklift.sh - 지게차 운전자 단말 (라즈베리파이/Linux) 실행 스크립트
# 실행 경로: 프로젝트 루트 디렉터리 (~/VEDA_Final_project)

set -e
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET_DIR="$ROOT_DIR/forklift-device/qt"
EXE_PATH="$TARGET_DIR/build/operator_terminal"

if [ ! -f "$EXE_PATH" ]; then
    echo "[INFO] 실행 파일이 없어 빌드를 먼저 진행합니다..."
    "$ROOT_DIR/build_forklift.sh"
fi

echo "[INFO] 지게차 운전자 단말을 실행합니다..."
cd "$TARGET_DIR/build"
./operator_terminal "$@"
