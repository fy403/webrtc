#!/bin/bash
# =============================================================================
# run.bat - Windows launcher for the MCP server (stdio or SSE mode)
# =============================================================================

@echo off
setlocal

cd /d "%~dp0"

if not exist "dist\index.js" (
    echo [ERROR] Server not built. Run: npm install ^&^& npm run build
    exit /b 1
)

if "%REMOTE_ID%"=="" echo [WARN] REMOTE_ID not set - must call device_connect manually
if "%SIGNALING_URL%"=="" set SIGNALING_URL=119.45.178.251
if "%SIGNALING_PORT%"=="" set SIGNALING_PORT=8000
if "%MCP_TRANSPORT%"=="" set MCP_TRANSPORT=sse
if "%MCP_SSE_PORT%"=="" set MCP_SSE_PORT=3000
if "%MCP_SSE_HOST%"=="" set MCP_SSE_HOST=0.0.0.0

echo ============================================
echo   Remote Control MCP Server
echo ============================================
echo   Signaling: %SIGNALING_URL%:%SIGNALING_PORT%
echo   Remote ID: %REMOTE_ID% (use device_connect to set)
echo   Transport: %MCP_TRANSPORT%

if "%MCP_TRANSPORT%"=="sse" (
    echo   SSE:       http://%MCP_SSE_HOST%:%MCP_SSE_PORT%
    echo.
    echo   Endpoints:
    echo     GET  /sse    - SSE stream
    echo     POST /messages?sessionId=.. - Client messages
    echo     GET  /health - Health check
    echo.
    echo   For MCP client config, use URL:
    echo     http://%MCP_SSE_HOST%:%MCP_SSE_PORT%/sse
) else (
    echo   Mode: stdio ^(for Claude Desktop integration^)
    echo   To enable SSE: set MCP_TRANSPORT=sse
)
echo ============================================

node dist/index.js
