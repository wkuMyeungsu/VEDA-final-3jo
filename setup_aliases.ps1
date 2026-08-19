# setup_aliases.ps1 - PowerShell 단축 명령어(Alias) 등록 스크립트
# 사용법: . .\setup_aliases.ps1 (현재 세션에 단축어 등록)

$RootDir = $PSScriptRoot

# 1. 관제 센터 (PC)
function global:run-operator {
    param([switch]$NoDemo)
    & (Join-Path $RootDir "run_operator.ps1") -NoDemo:$NoDemo
}
function global:build-operator {
    & (Join-Path $RootDir "run_operator.ps1") -BuildOnly
}

# 2. 지게차 운전자 단말 (지게차 디바이스)
function global:run-forklift {
    param([string]$Camera = "CAM_01", [switch]$NoDemo)
    & (Join-Path $RootDir "run_forklift.ps1") -Camera $Camera -NoDemo:$NoDemo
}
function global:build-forklift {
    & (Join-Path $RootDir "run_forklift.ps1") -BuildOnly
}

# 짧은 약칭 (Shortcuts)
Set-Alias -Name ro -Value global:run-operator -Scope Global -Option AllScope
Set-Alias -Name bo -Value global:build-operator -Scope Global -Option AllScope
Set-Alias -Name rf -Value global:run-forklift -Scope Global -Option AllScope
Set-Alias -Name bf -Value global:build-forklift -Scope Global -Option AllScope

Write-Host "===========================================================" -ForegroundColor Cyan
Write-Host "  ✨ 지게차 & 관제 센터 장비별 단축어 등록 완료!            " -ForegroundColor Green
Write-Host "===========================================================" -ForegroundColor Cyan
Write-Host "  [🖥️ 관제 센터 (PC)]" -ForegroundColor Yellow
Write-Host "    ▶ ro  (또는 run-operator)   : 관제 센터 데모 실행"
Write-Host "    ▶ bo  (또는 build-operator) : 관제 센터 빌드"
Write-Host "  [🚜 지게차 단말 (디바이스)]" -ForegroundColor Yellow
Write-Host "    ▶ rf  (또는 run-forklift)   : 지게차 단말 데모 실행"
Write-Host "    ▶ bf  (또는 build-forklift) : 지게차 단말 빌드"
Write-Host "===========================================================" -ForegroundColor Cyan
