# run_forklift.ps1 - 지게차 운전자 단말 원클릭 실행 스크립트
param(
    [switch]$BuildOnly,
    [string]$Camera = "CAM_01_CH_01",
    [switch]$NoDemo,
    [switch]$Mock
)

$ErrorActionPreference = "Stop"
$RootDir = $PSScriptRoot
$QtDir = Join-Path $RootDir "forklift-device\qt"
$ExePath = Join-Path $QtDir "build\windows-mingw\operator_terminal.exe"

# 1. Qt, 컴파일러, GStreamer, Mosquitto PATH 등록
$QtBin = "C:\Qt\6.11.0\mingw_64\bin"
$MinGWBin = "C:\Qt\Tools\mingw1310_64\bin"
$NinjaBin = "C:\Qt\Tools\Ninja"
$CMakeBin = "C:\Qt\Tools\CMake_64\bin"
$GStreamerBin = "C:\Program Files\gstreamer\1.0\mingw_x86_64\bin"
$MosquittoBin = "C:\Program Files\mosquitto"

$env:PATH = "$CMakeBin;$NinjaBin;$MinGWBin;$QtBin;$GStreamerBin;$MosquittoBin;$env:PATH"

# 2. 실행 파일 존재 여부 확인 및 자동 빌드
if (!(Test-Path $ExePath) -or $BuildOnly) {
    Write-Host "[INFO] operator_terminal 빌드를 시작합니다..." -ForegroundColor Cyan
    Push-Location $QtDir
    try {
        if (!(Test-Path "build\windows-mingw\CMakeCache.txt")) {
            cmake --preset windows-mingw
        }
        cmake --build --preset windows-mingw
    } finally {
        Pop-Location
    }
    Write-Host "[INFO] 빌드가 완료되었습니다." -ForegroundColor Green
    if ($BuildOnly) { return }
}

# 3. 프로그램 실행
$ArgsList = @()
if (!$NoDemo) {
    $ArgsList += "--demo"
}
if ($Mock) {
    $ArgsList += "--mock"
}
if ($Camera) {
    $ArgsList += "--camera"
    $ArgsList += $Camera
}

Write-Host "[INFO] 지게차 운전자 단말을 실행합니다 ($ArgsList)..." -ForegroundColor Green
Push-Location (Join-Path $QtDir "build\windows-mingw")
try {
    $env:QML_IMPORT_PATH = "$((Get-Location).Path)\qml;$((Get-Location).Path)\..;$env:QML_IMPORT_PATH"
    & $ExePath @ArgsList
} finally {
    Pop-Location
}
