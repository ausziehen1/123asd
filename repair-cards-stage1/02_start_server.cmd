@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul
set "REPAIR_SERVER=build\mingw64\repair_server.exe"
if not exist "%REPAIR_SERVER%" set "REPAIR_SERVER=build\mingw-debug\repair_server.exe"
if not exist "%REPAIR_SERVER%" set "REPAIR_SERVER=cmake-build-debug\repair_server.exe"
if not exist "%REPAIR_SERVER%" (
  echo First build repair_server in CLion or run 01_build.cmd
  pause
  exit /b 1
)
"%REPAIR_SERVER%" --config "config.example.json"
pause
