#!/bin/bash
# WebRTC Receiver Startup Script
# ==================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="${SCRIPT_DIR}/build/webrtc_receiver"
CONFIG="${SCRIPT_DIR}/config.txt"

# Default arguments
ARGS=""
REMOTE_ID=""
SIGNALING_URL=""
VIDEO_DEVICE=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --remote-id|-r)
            REMOTE_ID="$2"
            shift 2
            ;;
        --signaling-url|-s)
            SIGNALING_URL="$2"
            shift 2
            ;;
        --video-device|-v)
            VIDEO_DEVICE="$2"
            shift 2
            ;;
        --config|-c)
            CONFIG="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -r, --remote-id ID       Remote peer ID to connect to"
            echo "  -s, --signaling-url URL  Signaling server URL"
            echo "  -v, --video-device DEV   V4L2 device node (default: /dev/video10)"
            echo "  -c, --config FILE        Config file path"
            echo "  -h, --help               Show this help"
            exit 0
            ;;
        *)
            ARGS="$ARGS $1"
            shift
            ;;
    esac
done

# Build command
CMD="${BINARY}"

if [ -n "$CONFIG" ]; then
    CMD="${CMD} --config ${CONFIG}"
fi

if [ -n "$REMOTE_ID" ]; then
    CMD="${CMD} --remote-id ${REMOTE_ID}"
fi

if [ -n "$SIGNALING_URL" ]; then
    CMD="${CMD} --signaling-url ${SIGNALING_URL}"
fi

if [ -n "$VIDEO_DEVICE" ]; then
    CMD="${CMD} --video-device ${VIDEO_DEVICE}"
fi

CMD="${CMD} ${ARGS}"

# Check if binary exists
if [ ! -f "${BINARY}" ]; then
    echo "[!] Error: Binary not found at ${BINARY}"
    echo "    Please run './install.sh build' first to compile."
    exit 1
fi

# Check v4l2loopback module
if ! lsmod | grep -q v4l2loopback; then
    echo "[!] Warning: v4l2loopback module not loaded."
    echo "    Please run './install.sh v4l2' or: sudo modprobe v4l2loopback video_nr=10 exclusive_caps=1"
    echo ""
fi

echo "Starting WebRTC Receiver..."
echo "Binary: ${BINARY}"
echo "Config: ${CONFIG}"
echo ""

exec ${CMD}
