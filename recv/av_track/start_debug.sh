#!/bin/bash
# Debug launcher for WebRTC receiver
# Run: bash start_debug.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="${SCRIPT_DIR}/build/webrtc_receiver"
CONFIG="${SCRIPT_DIR}/config.txt"
LOG="/tmp/recv_debug.log"

echo "========================================="
echo " WebRTC Receiver Debug Launcher"
echo "========================================="

# 1. Kill old instances
pkill -f webrtc_receiver 2>/dev/null && echo "[1] Killed old receiver" || echo "[1] No old receiver"

# 2. Check v4l2loopback
echo "[2] V4L2 devices:"
ls -la /dev/video* 2>&1
v4l2-ctl --list-devices 2>&1 | head -10

# 3. Check PulseAudio
echo "[3] PulseAudio status:"
pactl info 2>&1 | head -3
pactl list short sinks 2>&1 | head -5

# 4. Start receiver with full output
echo "[4] Starting receiver to ${LOG}..."
echo "    Binary: ${BINARY}"
echo "    Config: ${CONFIG}"
echo ""
echo "--- STARTING (Ctrl+C to stop) ---"
cd "${SCRIPT_DIR}" && exec "${BINARY}" --config "${CONFIG}" 2>&1 | tee "${LOG}"
