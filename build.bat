@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo  Compiling FocusShield Native C++ Windows Executable
echo ========================================================

:: Step 1: Compile Resource Manifest
echo [1/2] Compiling Application Manifest and Visual Styles...
windres resources.rc -O coff -o resources.res
if %errorlevel% neq 0 (
    echo [ERROR] windres failed!
    exit /b %errorlevel%
)

:: Step 2: Compile C++ Binary with MinGW g++
echo [2/2] Compiling FocusShield.exe with g++...
g++ -O2 -std=c++11 src/main.cpp resources.res -o FocusShield.exe -mwindows -lws2_32 -lcomctl32 -lshlwapi -lgdiplus -static-libgcc -static-libstdc++
if %errorlevel% neq 0 (
    echo [ERROR] g++ compilation failed!
    exit /b %errorlevel%
)

:: Step 3: Strip debug symbols for small binary
strip FocusShield.exe

echo.
echo ========================================================
echo  SUCCESS: FocusShield.exe created successfully!
echo ========================================================

exit /b 0
