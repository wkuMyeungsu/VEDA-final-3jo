@echo off
setlocal
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;%PATH%"

cd /d "%~dp0operator-device\qt"

echo [INFO] Building Control Center (operator-device)...
if not exist "build\windows-mingw\CMakeCache.txt" (
    cmake --preset windows-mingw
)
cmake --build --preset windows-mingw

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Control Center build finished! Output: operator-device\qt\build\windows-mingw\control_center.exe
) else (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%
)
