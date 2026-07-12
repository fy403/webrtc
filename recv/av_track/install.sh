#!/bin/bash
# WebRTC Receiver Installation Script for Ubuntu/Linux
# =====================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "========================================"
echo " WebRTC Virtual Camera Receiver Installer"
echo "========================================"
echo ""

# Check if running as root for v4l2loopback module loading
check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "[!] Warning: Not running as root."
        echo "    Some operations (v4l2loopback module) require root privileges."
        echo ""
        return 1
    fi
    return 0
}

# Install system dependencies
install_dependencies() {
    echo "[*] Installing system dependencies..."
    
    sudo apt-get update
    
    # Build tools
    sudo apt-get install -y \
        build-essential \
        cmake \
        pkg-config \
        git \
        nlohmann-json3-dev
    
    # FFmpeg development libraries
    sudo apt-get install -y \
        libavformat-dev \
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        libswresample-dev
    
    # V4L2 development libraries (for v4l2loopback output)
    sudo apt-get install -y \
        libv4l-dev
    
    # PulseAudio development libraries
    sudo apt-get install -y \
        libpulse-dev
    
    # OpenSSL (required by libdatachannel)
    sudo apt-get install -y \
        libssl-dev
    
    # v4l2loopback kernel module
    echo "[*] Installing v4l2loopback kernel module..."
    sudo apt-get install -y v4l2loopback-dkms || {
        echo "[!] v4l2loopback-dkms installation failed, trying alternative..."
        sudo apt-get install -y v4l2loopback-utils || true
        
        # Try to build from source if package not available
        if ! modinfo v4l2loopback &>/dev/null; then
            echo "[*] Building v4l2loopback from source..."
            tmp_dir=$(mktemp -d)
            git clone https://github.com/umlaeute/v4l2loopback.git "$tmp_dir/v4l2loopback"
            cd "$tmp_dir/v4l2loopback"
            make && sudo make install
            depmod -a
            cd "$SCRIPT_DIR"
            rm -rf "$tmp_dir"
        fi
    }
    
    echo "[+] Dependencies installed successfully!"
}

# Install/build libdatachannel (if not already installed)
install_libdatachannel() {
    if ldconfig -p | grep -q libdatachannel; then
        echo "[*] libdatachannel already installed, skipping..."
        return 0
    fi
    
    echo "[*] Building libdatachannel from source..."
    
    tmp_dir=$(mktemp -d)
    git clone --recursive https://github.com/paullouisageneau/libdatachannel.git "$tmp_dir/datachannel"
    cd "$tmp_dir/datachannel"
    
    mkdir -p build && cd build
    cmake .. -DRTC_BUILD_EXAMPLES=OFF
    make -j$(nproc)
    sudo make install
    sudo ldconfig
    
    cd "$SCRIPT_DIR"
    rm -rf "$tmp_dir"
    
    echo "[+] libdatachannel installed successfully!"
}

# Build the project
build_project() {
    echo "[*] Building webrtc_receiver..."
    
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    
    cd "${SCRIPT_DIR}"
    
    echo "[+] Build completed! Binary: ${BUILD_DIR}/webrtc_receiver"
}

# Load v4l2loopback kernel module
load_v4l2loopback() {
    echo "[*] Loading v4l2loopback kernel module..."
    
    # Unload first if already loaded
    sudo modprobe -r v4l2loopback 2>/dev/null || true
    
    # Load with exclusive_caps=1 (important for some apps like Firefox/Chrome)
    # video_nr=10 uses device /dev/video10
    sudo modprobe v4l2loopback video_nr=10 exclusive_caps=1 card_label="WebRTC_Virtual_Camera"
    
    # Verify device was created
    sleep 1
    if [ -e "/dev/video10" ]; then
        echo "[+] v4l2loopback loaded successfully at /dev/video10"
    else
        echo "[!] Warning: /dev/video10 not found. Trying to find available device..."
        ls -la /dev/video*
        
        # Try without specifying device number
        sudo modprobe -r v4l2loopback 2>/dev/null || true
        sudo modprobe v4l2loopback exclusive_caps=1 card_label="WebRTC_Virtual_Camera"
        sleep 1
    fi
}

# Setup PulseAudio virtual sink
setup_pulseaudio() {
    echo "[*] Setting up PulseAudio virtual audio sink..."
    
    # Load virtual sink module
    PACTL_SINK=$(pactl load-module module-virtual-sink \
        sink_name=webrtc_receiver_sink \
        sink_properties=device.description="WebRTC_Receiver_Mic")
    
    if [ -n "$PACTL_SINK" ] && [ "$PACTL_SINK" -gt 0 ] 2>/dev/null; then
        echo "[+] PulseAudio virtual sink created (module ID: $PACTL_SINK)"
        echo "    Sink: webrtc_receiver_sink"
        echo "    Monitor source: webrtc_receiver_sink.monitor"
    else
        echo "[!] Failed to create PulseAudio virtual sink (PipeWire may be running?)"
        echo "    Audio output will attempt direct write to default device."
    fi
}

# Print usage instructions
print_usage() {
    echo ""
    echo "========================================"
    echo " Installation Complete!"
    echo "========================================"
    echo ""
    echo "Usage:"
    echo "  ./run.sh                          # Start with defaults"
    echo "  ./run.sh --remote-id DEVICE_ID   # Connect to specific device"
    echo "  ./build/webrtc_receiver          # Run binary directly"
    echo ""
    echo "Virtual Devices:"
    echo "  Video: /dev/video10 (v4l2loopback)"
    echo "  Audio: PulseAudio 'WebRTC_Receiver_Mic' sink"
    echo ""
    echo "To use with OBS:"
    echo "  Add Source -> Video Capture Device -> WebRTC_Virtual_Camera"
    echo ""
    echo "To use with GStreamer/ffmpeg:"
    echo "  gst-launch-1.0 v4l2src device=/dev/video10 ! ... "
    echo ""
}

# Main installation flow
main() {
    case "${1:-all}" in
        deps)
            install_dependencies
            ;;
        libdatachannel)
            install_libdatachannel
            ;;
        build)
            build_project
            ;;
        v4l2)
            load_v4l2loopback
            ;;
        pulseaudio)
            setup_pulseaudio
            ;;
        all|*)
            install_dependencies
            install_libdatachannel
            build_project
            load_v4l2loopback
            setup_pulseaudio
            print_usage
            ;;
    esac
}

main "$@"
