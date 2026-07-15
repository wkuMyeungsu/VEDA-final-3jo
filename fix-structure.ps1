# =========================================================
# VEDA-final-3jo : develop 브랜치 apps/, qml/ 구조 정리 스크립트
# 실행 위치: 리포 루트 (C:\Repos\veda-final-3jo)
# 실행 전: git status로 미커밋 변경사항 없는지 확인!
# =========================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Write-Host "=== 0. develop 최신화 ===" -ForegroundColor Cyan
git switch develop
git pull

Write-Host "`n=== 1. 대상 폴더 생성 ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path "operator-device\apps" | Out-Null
New-Item -ItemType Directory -Force -Path "operator-device\qml"  | Out-Null
New-Item -ItemType Directory -Force -Path "forklift-device\apps" | Out-Null
New-Item -ItemType Directory -Force -Path "forklift-device\qml"  | Out-Null

Write-Host "`n=== 2. git mv로 이동 (히스토리 보존) ===" -ForegroundColor Cyan
git mv apps\control_center     operator-device\apps\control_center
git mv qml\control_center      operator-device\qml\control_center

git mv apps\operator_terminal  forklift-device\apps\operator_terminal
git mv qml\operator_terminal   forklift-device\qml\operator_terminal

Write-Host "`n=== 3. 빈 폴더 정리 (apps/, qml/ 루트에 남은 게 있으면 확인) ===" -ForegroundColor Cyan
Get-ChildItem apps -ErrorAction SilentlyContinue
Get-ChildItem qml  -ErrorAction SilentlyContinue
Write-Host "위 목록이 비어있어야 정상. qml/theme, qml/components는 그대로 남아있는 게 맞음." -ForegroundColor Yellow

Write-Host "`n=== 4. 완료. 다음은 CMakeLists.txt 수동 수정 필요 ===" -ForegroundColor Green
Write-Host "  - CMakeLists.txt (루트)"
Write-Host "  - operator-device\apps\control_center\CMakeLists.txt"
Write-Host "  - forklift-device\apps\operator_terminal\CMakeLists.txt"
Write-Host "수정 후 CMake reconfigure + 빌드로 검증하세요."
