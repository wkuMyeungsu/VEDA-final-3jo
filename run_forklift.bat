@echo off
setlocal
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;C:\Program Files\gstreamer\1.0\mingw_x86_64\bin;C:\Program Files\mosquitto;%PATH%"

cd /d "%~dp0forklift-device\qt"

if not exist "build\windows-mingw\operator_terminal.exe" (
    echo [INFO] Building operator_terminal...
    if not exist "build\windows-mingw\CMakeCache.txt" (
        cmake --preset windows-mingw
    )
    cmake --build --preset windows-mingw
)

echo [INFO] Starting Forklift Operator Terminal (Demo Mode)...
cd /d "%~dp0forklift-device\qt\build\windows-mingw"
set "QML_IMPORT_PATH=%CD%\qml;%CD%\..;%QML_IMPORT_PATH%"
start operator_terminal.exe --demo --camera CAM_01_CH_01
