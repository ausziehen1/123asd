@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul
if errorlevel 1 goto missing
where gcc >nul 2>nul
if errorlevel 1 goto missing
where g++ >nul 2>nul
if errorlevel 1 goto missing
where mingw32-make >nul 2>nul
if errorlevel 1 goto missing
cmake -S . -B build/mingw64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 goto failed
cmake --build build/mingw64 --parallel 2
if errorlevel 1 goto failed
cmake --build build/mingw64 --target test
if errorlevel 1 goto failed
echo Build and core tests passed. Next: 02_start_server.cmd
pause
exit /b 0
:missing
echo CMake and MinGW64 bin directories must be in PATH for this optional script.
echo Alternatively, build directly in CLion. See docs/CLION.md
:failed
echo Build did not complete. Please copy the error text.
pause
exit /b 1
