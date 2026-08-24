@echo off
setlocal
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;C:\Program Files\Mosquitto;C:\Program Files\gstreamer\1.0\mingw_x86_64\bin;%PATH%"

cd /d "%~dp0forklift-device\qt"

echo [INFO] Building Forklift Operator Terminal (forklift-device)...
if not exist "build\windows-mingw\CMakeCache.txt" (
    cmake --preset windows-mingw
)
cmake --build --preset windows-mingw

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Forklift Terminal build finished! Output: forklift-device\qt\build\windows-mingw\operator_terminal.exe
) else (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%
)
