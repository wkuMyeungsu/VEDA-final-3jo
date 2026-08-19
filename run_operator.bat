@echo off
setlocal
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;C:\Program Files\gstreamer\1.0\mingw_x86_64\bin;C:\Program Files\mosquitto;%PATH%"

cd /d "%~dp0operator-device\qt"

if not exist "build\windows-mingw\control_center.exe" (
    echo [INFO] Building control_center...
    if not exist "build\windows-mingw\CMakeCache.txt" (
        cmake --preset windows-mingw
    )
    cmake --build --preset windows-mingw
)

echo [INFO] Starting Control Center (Demo Mode)...
echo [INFO] Login ID: hanwha / PIN: 5hanwha!
cd /d "%~dp0operator-device\qt\build\windows-mingw"
set "QML_IMPORT_PATH=%CD%\qml;%CD%\..;%QML_IMPORT_PATH%"
start control_center.exe --demo
