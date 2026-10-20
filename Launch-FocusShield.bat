@echo off
setlocal enabledelayedexpansion
title FocusShield - Network Level Website Blocker

:: Check for administrative rights
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [FocusShield] Requesting Administrator privileges to manage network blocks...
    powershell -NoProfile -Command "Start-Process cmd.exe -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

:: Navigate to project root
cd /d "%~dp0"

echo [FocusShield] Starting FocusShield Local Server...

:: Check if server is already running on port 52771
netstat -ano | findstr :52771 >nul 2>&1
if %errorlevel% neq 0 (
    start /b python server.py
    :: Wait 1.5 seconds for server to initialize
    timeout /t 2 /nobreak >nul
)

:: Detect Edge or Chrome to launch in seamless native app window mode
set BROWSER_CMD=

if exist "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" (
    set "BROWSER_CMD=C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
) else if exist "C:\Program Files\Google\Chrome\Application\chrome.exe" (
    set "BROWSER_CMD=C:\Program Files\Google\Chrome\Application\chrome.exe"
)

if defined BROWSER_CMD (
    echo [FocusShield] Launching standalone app window...
    start "" "!BROWSER_CMD!" --app="http://127.0.0.1:52771" --window-size=1280,860
) else (
    echo [FocusShield] Opening in default browser...
    start http://127.0.0.1:52771
)

exit /b
