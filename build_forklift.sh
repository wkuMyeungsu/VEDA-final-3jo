#!/bin/bash
# build_forklift.sh - 지게차 운전자 단말 (라즈베리파이/Linux) 빌드 스크립트
# 실행 경로: 프로젝트 루트 디렉터리 (~/VEDA_Final_project)

set -e
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET_DIR="$ROOT_DIR/forklift-device/qt"

echo "[INFO] 지게차 운전자 단말(forklift-device) 빌드를 시작합니다..."
cd "$TARGET_DIR"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

echo "[SUCCESS] 빌드 완료! 산출물: $TARGET_DIR/build/operator_terminal"
