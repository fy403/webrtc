@echo off
chcp 65001 >nul 2>&1
title WebRTC Remote Control System - Local Server

echo ========================================
echo   WebRTC Remote Control System
echo ========================================
echo.

cd /d "%~dp0web"

:: Kill old process on port 8080
echo [1/3] Checking port 8080...
for /f "tokens=5" %%a in ('netstat -aon ^| findstr :8080 ^| findstr LISTENING') do (
    echo     Found process PID: %%a, terminating...
    taskkill /PID %%a /F >nul 2>&1
)
timeout /t 1 /nobreak >nul
echo     ✅ Port 8080 is free

:: Start HTTP server (port 8080)
echo.
echo [2/3] Starting HTTP server...
start "WebRTC-Server" python -m http.server 8080
timeout /t 1 /nobreak >nul
echo     ✅ Server running at: http://localhost:8080

:: Open default browser
echo.
echo [3/3] Opening browser...
start http://localhost:8080/index.html
echo     ✅ Done!

echo.
echo ========================================
echo   Press Ctrl+C to stop the server
echo ========================================
pause
