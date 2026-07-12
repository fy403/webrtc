#!/bin/bash
# =============================================================================
# run.bat - Windows launcher for the MCP server
# =============================================================================

@echo off
setlocal

cd /d "%~dp0"

if not exist "dist\index.js" (
    echo [ERROR] Server not built. Run: npm install && npm run build
    exit /b 1
)

if "%REMOTE_ID%"=="" echo [WARN] REMOTE_ID not set - must call device_connect manually
if "%SIGNALING_URL%"=="" set SIGNALING_URL=119.45.178.251
if "%SIGNALING_PORT%"=="" set SIGNALING_PORT=8000

echo ============================================
echo   Remote Control MCP Server
echo ============================================
echo   Signaling: %SIGNALING_URL%:%SIGNALING_PORT%
echo   Remote ID: %REMOTE_ID% (use device_connect to set)
echo ============================================

node dist/index.js
