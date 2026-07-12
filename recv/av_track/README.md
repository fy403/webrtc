# WebRTC Virtual Camera Receiver (Ubuntu/Linux)

A C++ native WebRTC receiver that captures H.264/H.265+Opus streams from a remote device (e.g., RC car with camera) and presents them as local virtual devices:

- **Virtual Camera**: `/dev/videoN` via v4l2loopback kernel module
- **Virtual Microphone**: PulseAudio virtual sink

Third-party applications (OBS, Zoom, GStreamer, etc.) can directly use these virtual devices as if they were real hardware.

## Features

- ✅ **WebRTC P2P Reception**: Uses `libdatachannel` for low-latency peer-to-peer media streaming
- ✅ **H.264/H.265 Video**: Full FFmpeg hardware/software decoding support
- ✅ **Opus Audio**: High-quality audio decoding with automatic resampling
- ✅ **v4l2loopback Output**: System-level virtual webcam visible to all apps
- ✅ **PulseAudio Output**: Virtual audio source for microphone simulation
- ✅ **Auto-Reconnect**: Automatic reconnection on network/ICE failures
- ✅ **Latency Monitoring**: Real-time E2E delay statistics (decode/V4L2/PA)
- ✅ **Low-Latency Optimized**: Sub-100ms target latency

## Architecture

```
Remote Device (av_track)
    │
    │◄── P2P WebRTC RTP ─────────────►│
    │  (H264/H265 + Opus)             │
                                     ▼
                            recv.exe (Ubuntu)
                          ┌─────────────────┐
                          │  WebRTCReceiver  │
                          └────┬──────┬──────┘
                     onTrack│      │onTrack
                   ┌────────┘      └────────┐
                   ▼                       ▼
            VideoPipeline           AudioPipeline
            (RTP→Decode→RGB24)     (RTP→Decode→PCM)
                   ▼                       ▼
          /dev/video10          PulseAudio Sink
        (v4l2loopback)       (virtual-mic)
                   │                       │
                   └───────────┬───────────┘
                              ▼
                    Third-party Apps
                  (OBS / Zoom / GStreamer)
```

## Prerequisites

### System Requirements

- Ubuntu 18.04+ or compatible Linux distribution
- Kernel with v4l2loopback module support
- PulseAudio or PipeWire audio server
- CMake 3.10+, GCC/Clang with C++17 support

### Install Dependencies

```bash
# Quick install using provided script
./install.sh deps

# Or manually:
sudo apt-get update && sudo apt-get install -y \
    build-essential cmake pkg-config git nlohmann-json3-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev \
    libv4l-dev libv4l2-dev libpulse-dev libssl-dev

# v4l2loopback kernel module
sudo apt-get install -y v4l2loopback-dkms || {
    # Build from source if package unavailable
    git clone https://github.com/umlaeute/v4l2loopback.git /tmp/v4l2loopback
    cd /tmp/v4l2loopback && make && sudo make install && sudo depmod -a
}
```

### Build libdatachannel

```bash
# Using install script
./install.sh libdatachannel

# Or manually:
git clone --recursive https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel
mkdir build && cd build
cmake .. -DRTC_BUILD_EXAMPLES=OFF
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Building

```bash
# Full installation (build + setup v4l2 + pulseaudio)
./install.sh all

# Or step by step:
./install.sh build      # Compile only
./install.sh v4l2       # Load v4l2loopback module
./install.sh pulseaudio # Setup PA virtual sink
```

**Manual build:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Output binary: `build/webrtc_receiver`

## Usage

### Basic Start

```bash
# With remote device ID
./run.sh --remote-id ABC123

# Custom signaling server
./run.sh -r DEVICE_ID -s ws://your-server:8000

# Custom V4L2 device
./run.sh -r DEVICE_ID -v /dev/video5
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-r, --remote-id ID` | Remote peer ID to connect to | *(required)* |
| `-s, --signaling-url URL` | Signaling server WebSocket URL | `ws://tx.fy403.cn:8000` |
| `-v, --video-device DEV` | V4L2 loopback device path | `/dev/video10` |
| `-c, --config FILE` | Configuration file path | `config.txt` |
| `-h, --help` | Show help message | - |

### Configuration File (`config.txt`)

Edit `config.txt` for persistent settings:

```ini
[signaling]
url = ws://your-signaling-server:8000

[ice]
stun_server = stun.l.google.com:19302
turn_server = turn:server:3478?transport=udp
turn_username = your_user
turn_password = your_pass

[connection]
remote_id =
auto_reconnect = true

[video]
video_device = /dev/video10
video_format = RGB24

[audio]
sink_name = webrtc_receiver_sink
sample_rate = 48000
channels = 2
```

## Virtual Device Setup

### Load v4l2loopback Module

```bash
# Create virtual video device at /dev/video10 with exclusive caps
sudo modprobe v4l2loopback video_nr=10 exclusive_caps=1 card_label="WebRTC_Camera"

# Verify
ls -la /dev/video10

# To unload (when done)
sudo modprobe -r v4l2loopback
```

> ⚠️ **Note**: `exclusive_caps=1` is important! It allows Firefox/Chrome/OBS to recognize the device as a real webcam.

### Setup PulseAudio Virtual Sink

```bash
# Create virtual audio sink (microphone simulation)
pactl load-module module-virtual-sink \
    sink_name=webrtc_receiver_sink \
    sink_properties=device.description="WebRTC_Microphone"

# List sinks to verify
pactl list sinks short

# To remove (get module ID from above command output)
pactl unload-module <module_id>
```

### Verify Virtual Devices

```bash
# Check video device
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video10 --info

# Check audio sink
pactl list sinks
pactl list sources
```

## Integration with Applications

### OBS Studio

1. Add Source → Video Capture Device → Select "WebRTC_Virtual_Camera"
2. For audio: Audio Input Capture → Select "WebRTC_Receiver_Mic.monitor"

### GStreamer / FFmpeg

```bash
# GStreamer capture
gst-launch-1.0 v4l2src device=/dev/video10 ! videoconvert ! autovideosink

# FFmpeg capture
ffmpeg -f video4linux2 -i /dev/video10 output.mp4

# FFmpeg + audio (from PulseAudio monitor)
ffmpeg -f video4linux2 -i /dev/video10 -f pulse -name webrtc_receiver_sink.monitor output.mkv
```

### Python (OpenCV)

```python
import cv2

cap = cv2.VideoCapture('/dev/video10')
while True:
    ret, frame = cap.read()
    if not ret:
        break
    cv2.imshow('WebRTC Stream', frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break
cap.release()
cv2.destroyAllWindows()
```

## Performance Tuning

### Low Latency Configuration

For sub-100ms E2E latency, ensure:

1. **Network**: Wired connection preferred over WiFi; <50ms RTT to TURN server
2. **Sender (av_track)**: Set high bitrate (4-8 Mbps), enable hardware encoding if available
3. **Receiver (recv)**:
   - Use `V4L2_PIX_FMT_RGB24` format (fastest conversion)
   - Keep `config.txt` stats interval ≥1s to avoid overhead
   - Run with high process priority: `nice -n -20 ./webrtc_receiver ...`

### Troubleshooting

**Problem**: "Device busy" when opening /dev/video10
- Solution: Close other apps using the device; check with `lsof /dev/video10`

**Problem**: PulseAudio connection failed
- Solution: Ensure PulseAudio is running (`pulseaudio --check`); try restarting PA daemon

**Problem**: High latency (>200ms)
- Solutions:
  1. Check network RTT (ping TURN server)
  2. Reduce resolution on sender side
  3. Ensure hardware acceleration on both ends
  4. Check CPU load (`top` or `htop`)

**Problem**: No video/audio after connection established
- Solutions:
  1. Verify remote device (av_track) is actually sending media
  2. Check ICE connection state in console output
  3. Try manual SDP exchange debug mode

## Project Structure

```
recv/
├── CMakeLists.txt              # Main build configuration
├── config.txt                  # Default settings
├── install.sh                  # Installation script
├── run.sh                      # Startup wrapper
├── README.md                   # This file
│
├── include/                    # Headers
│   ├── config_parser.h         # Config file parser
│   ├── signaling_client.h      # WebSocket signaling
│   ├── webrtc_receiver.h       # Core WebRTC logic
│   ├── video_pipeline.h        # H264/H265 decode + V4L2 out
│   ├── audio_pipeline.h        # Opus decode + PA out
│   ├── v4l2_output.h           # V4L2 device abstraction
│   ├── pulse_audio_output.h    # PulseAudio wrapper
│   ├── frame_buffer.h          # Thread-safe queue template
│   └── latency_tracker.h       # Latency measurement
│
└── src/                        # Implementation
    ├── main.cpp                # Entry point & CLI
    ├── config_parser.cpp       # Config parsing logic
    ├── signaling_client.cpp    # WS connection handler
    ├── webrtc_receiver.cpp     # PC management & callbacks
    ├── video_pipeline.cpp      # Video decode thread
    ├── audio_pipeline.cpp      # Audio decode thread
    ├── v4l2_output.cpp         # V4L2 ioctl operations
    ├── pulse_audio_output.cpp  # PA simple API calls
    └── latency_tracker.cpp     # Stats calculation
```

## License

MIT License - see parent project LICENSE file.

## Credits

- **libdatachannel**: [paullouisageneau/libdatachannel](https://github.com/paullouisageneau/libdatachannel) - C++ WebRTC library
- **v4l2loopback**: [umlaeute/v4l2loopback](https://github.com/umlaeute/v4l2loopback) - Virtual video device module
- **FFmpeg**: Multimedia framework for decoding
- Original project: [fy403/webrtc](https://github.com/fy403/webrtc) - Remote control car system

---

**Happy streaming! 🎥🎤**
