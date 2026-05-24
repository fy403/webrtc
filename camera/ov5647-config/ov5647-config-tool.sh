#!/bin/bash
# OV5647 Camera Configuration Tool (All-in-One)
# Configures OV5647 sensor for optimal resolution and frame rate on Rockchip platforms
#
# Usage: ./ov5647-config-tool.sh [OPTIONS]
# Author: Assistant
# Version: 2.0 (Single File Version)
# Date: 2026-05-24

set -e

# ============================================================================
# Configuration
# ============================================================================

# Default settings (can be overridden by command line or config file)
DEFAULT_RESOLUTION="1920x1080"
DEFAULT_FRAMERATE="30"
CONFIG_FILE="/etc/default/ov5647"
LOG_FILE="/var/log/ov5647-config.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# Logging Functions
# ============================================================================

log() {
    local msg="[INFO] $(date '+%Y-%m-%d %H:%M:%S') - $1"
    echo -e "${GREEN}${msg}${NC}"
    echo "$msg" >> "$LOG_FILE"
    logger -t ov5647-config "$1" 2>/dev/null || true
}

warn() {
    local msg="[WARN] $(date '+%Y-%m-%d %H:%M:%S') - $1"
    echo -e "${YELLOW}${msg}${NC}"
    echo "$msg" >> "$LOG_FILE"
    logger -t ov5647-config "$1" 2>/dev/null || true
}

error() {
    local msg="[ERROR] $(date '+%Y-%m-%d %H:%M:%S') - $1"
    echo -e "${RED}${msg}${NC}" >&2
    echo "$msg" >> "$LOG_FILE"
    logger -t ov5647-config "$1" 2>/dev/null || true
}

debug() {
    if [ "$VERBOSE" = "true" ]; then
        local msg="[DEBUG] $(date '+%Y-%m-%d %H:%M:%S') - $1"
        echo -e "${BLUE}${msg}${NC}"
        echo "$msg" >> "$LOG_FILE"
    fi
}

# ============================================================================
# Display Functions
# ============================================================================

print_banner() {
    cat << 'EOF'
╔══════════════════════════════════════════════════════════════════════╗
║          OV5647 Camera Configuration Tool v2.0                         ║
║          (All-in-One Single File Version)                              ║
║                                                                       ║
║  Supports: Raspberry Pi Camera v1.3 (OV5647) on Rockchip Platforms  ║
╚══════════════════════════════════════════════════════════════════════╝
EOF
}

usage() {
    print_banner
    cat << EOF

Usage: $0 [OPTIONS]

Resolution and Frame Rate Options:
  -r, --resolution RESOLUTION   Set resolution (default: 1920x1080)
                                Supported: 2592x1944, 1920x1080, 1280x720, 640x480
  -f, --framerate FPS           Set frame rate (default: auto)
                                Auto values: 15, 30, 60, 90 (by resolution)
  
Action Options:
  -l, --list                    List all supported resolutions and frame rates
  -c, --check                   Check current configuration
  -t, --test                    Test actual capture frame rate
  -i, --install                 Install as system service (requires root)
  -u, --uninstall               Uninstall system service (requires root)
  
Image Quality Options:
  -q, --quality                 Configure image quality (exposure, gain, etc.)
  -I, --image-settings          Show current image quality settings

Service Management:
  -s, --status                  Show service status
  -S, --start                   Start service
  -T, --stop                    Stop service
  -R, --restart                 Restart service
  
Other Options:
  -v, --verbose                 Enable verbose output
  -h, --help                    Display this help message
  --dry-run                      Show what would be done without making changes

Examples:
  $0 -r 640x480                Configure for 640x480 resolution
  $0 -r 1280x720 -f 60         Configure for 720p @ 60fps
  $0 -l                         List all supported modes
  $0 -c                         Check current sensor configuration
  $0 -t                         Test actual capture frame rate
  $0 -q                         Configure image quality
  $0 -i                         Install as system service
  $0 -v -r 1920x1080           Verbose mode

EOF
}

list_modes() {
    print_banner
    echo ""
    echo "Supported Resolutions and Frame Rates for OV5647:"
    echo ""
    printf "${GREEN}%-20s %-20s %-30s${NC}\n" "Resolution" "Max FPS" "Description"
    printf "%-20s %-20s %-30s\n" "--------------------" "--------------------" "-------------------------------"
    printf "%-20s %-20s %-30s\n" "2592x1944" "15" "5 Megapixel (Full resolution)"
    printf "%-20s %-20s %-30s\n" "1920x1080" "30" "1080p Full HD (Default)"
    printf "%-20s %-20s %-30s\n" "1280x720" "45" "720p HD"
    printf "%-20s %-20s %-30s\n" "640x480" "60" "VGA (480p)"
    echo ""
    echo "Note: Actual frame rate may be limited by MIPI bandwidth and lighting conditions"
    echo ""
}

# ============================================================================
# Core Functions
# ============================================================================

# Parse resolution to width and height
parse_resolution() {
    local res=$1
    WIDTH=$(echo "$res" | cut -dx -f1)
    HEIGHT=$(echo "$res" | cut -dx -f2)
    
    if [ -z "$WIDTH" ] || [ -z "$HEIGHT" ]; then
        error "Invalid resolution format: $res"
        error "Expected format: WIDTHxHEIGHT (e.g., 640x480)"
        exit 1
    fi
    
    # Validate resolution is supported
    case "$res" in
        "2592x1944"|"1920x1080"|"1280x720"|"640x480")
            debug "Resolution validated: $res"
            ;;
        *)
            error "Unsupported resolution: $res"
            error "Supported resolutions: 2592x1944, 1920x1080, 1280x720, 640x480"
            exit 1
            ;;
    esac
}

# Get max framerate for resolution
get_max_framerate() {
    local res=$1
    case "$res" in
        "2592x1944") echo "15" ;;
        "1920x1080") echo "30" ;;
        "1280x720") echo "45" ;;
        "640x480") echo "60" ;;
        *) echo "30" ;;
    esac
}

# Wait for camera to be ready
wait_for_camera() {
    log "Waiting for OV5647 camera to be ready..."
    
    local max_attempts=30
    local attempt=0
    
    while [ $attempt -lt $max_attempts ]; do
        # Check if media device exists
        if [ -e "/dev/media0" ]; then
            # Check if OV5647 is in the media topology
            if media-ctl -d /dev/media0 -p 2>/dev/null | grep -q "ov5647"; then
                log "Camera is ready"
                return 0
            fi
        fi
        
        attempt=$((attempt + 1))
        debug "Waiting for camera... attempt $attempt/$max_attempts"
        sleep 1
    done
    
    error "Timeout waiting for camera to be ready"
    return 1
}

# Find OV5647 sensor device
# NOTE: Only outputs the device path to stdout (for capture with $())
# All log messages go to stderr to avoid contaminating the output
find_sensor_device() {
    log "Searching for OV5647 sensor device..." >&2

    local sensor_dev=""

    # Method 1: Look for m00_b_ov5647 in media controller
    if media-ctl -d /dev/media0 -p 2>/dev/null | grep -q "m00_b_ov5647"; then
        debug "Found OV5647 in media controller" >&2
        # Get the v4l-subdev device for the sensor (device node name is 2 lines after entity)
        sensor_dev=$(media-ctl -d /dev/media0 -p 2>/dev/null | grep -A2 "m00_b_ov5647" | grep "device node name" | grep -o "/dev/v4l-subdev[0-9]" | head -1 || true)
    fi

    # Method 2: Try each subdev
    if [ -z "$sensor_dev" ]; then
        for dev in /dev/v4l-subdev*; do
            if [ -e "$dev" ]; then
                local info=$(v4l2-ctl -d "$dev" --info 2>/dev/null || true)
                if echo "$info" | grep -q "ov5647\|OmniVision"; then
                    sensor_dev="$dev"
                    break
                fi
            fi
        done
    fi

    # Method 3: Check all subdevs for camera entity
    if [ -z "$sensor_dev" ]; then
        for dev in /dev/v4l-subdev*; do
            if [ -e "$dev" ]; then
                local entity_name=$(v4l2-ctl -d "$dev" --info 2>/dev/null | grep "Camera" || true)
                if [ -n "$entity_name" ]; then
                    sensor_dev="$dev"
                    break
                fi
            fi
        done
    fi

    if [ -z "$sensor_dev" ]; then
        error "Could not find OV5647 sensor sub-device" >&2
        error "Make sure the camera is connected and drivers are loaded" >&2
        return 1
    fi

    log "Found OV5647 sensor at: $sensor_dev" >&2
    echo "$sensor_dev"
    return 0
}

# Stop rkaiq service if running
stop_rkaiq() {
    if systemctl is-active --quiet rkaiq_3A.service 2>/dev/null; then
        log "Stopping rkaiq_3A service..."
        systemctl stop rkaiq_3A.service
        sleep 1
    fi
}

# Start rkaiq service
start_rkaiq() {
    if ! systemctl is-active --quiet rkaiq_3A.service 2>/dev/null; then
        log "Starting rkaiq_3A service..."
        systemctl start rkaiq_3A.service
    fi
}

# Configure image quality (brightness, exposure, gain, etc.)
# Parameters are tuned per resolution for optimal brightness
configure_image_quality() {
    local res="${1:-$WIDTHx$HEIGHT}"
    
    log "Configuring image quality settings for $res..."
    
    # Set resolution-specific parameters (tuned for OV5647 on Rockchip)
    local exposure_val analogue_gain_val
    
    case "$res" in
        "2592x1944")
            # Full resolution: longer exposure needed (more pixels, less light per pixel)
            exposure_val="1200"
            analogue_gain_val="300"
            log "Using full-resolution tuned parameters"
            ;;
        "1920x1080")
            # 1080p: balanced settings
            exposure_val="1000"
            analogue_gain_val="250"
            log "Using 1080p tuned parameters"
            ;;
        "1280x720")
            # 720p: brighter than default (user reported too dark with 800/200)
            exposure_val="1100"
            analogue_gain_val="350"
            log "Using 720p tuned parameters (increased brightness)"
            ;;
        "640x480")
            # 480p: much brighter (user reported very dark - likely sensor binning mode)
            exposure_val="1400"
            analogue_gain_val="500"
            log "Using 480p tuned parameters (high brightness mode)"
            ;;
        *)
            # Fallback: moderate brightness
            exposure_val="1000"
            analogue_gain_val="250"
            log "Using default image quality parameters"
            ;;
    esac
    
    # Enable auto exposure
    log "Enabling auto exposure (mode 0)..."
    v4l2-ctl -d /dev/video0 --set-ctrl=auto_exposure=0 2>&1 || warn "Could not set auto exposure"
    
    # Set exposure time (resolution-specific)
    log "Setting exposure to $exposure_val..."
    v4l2-ctl -d /dev/video0 --set-ctrl=exposure=$exposure_val 2>&1 || warn "Could not set exposure"
    
    # Set analogue gain (resolution-specific)
    log "Setting analogue gain to $analogue_gain_val..."
    v4l2-ctl -d /dev/video0 --set-ctrl=analogue_gain=$analogue_gain_val 2>&1 || warn "Could not set analogue gain"
    
    # Enable auto white balance
    log "Enabling auto white balance..."
    v4l2-ctl -d /dev/video0 --set-ctrl=white_balance_automatic=1 2>&1 || warn "Could not enable auto white balance"
    
    # Enable auto gain
    log "Enabling auto gain..."
    v4l2-ctl -d /dev/video0 --set-ctrl=gain_automatic=1 2>&1 || warn "Could not enable auto gain"
    
    # Try to set additional image quality parameters if available
    v4l2-ctl -d /dev/video0 --set-ctrl=saturation=150 2>&1 || debug "Saturation control not available"
    v4l2-ctl -d /dev/video0 --set-ctrl=contrast=150 2>&1 || debug "Contrast control not available"
    
    log "Image quality configuration complete!"
    log "Parameters applied: exposure=$exposure_val, gain=$analogue_gain_val"
    log "If still too dark, try:"
    log "  v4l2-ctl -d /dev/video0 --set-ctrl=exposure=$((exposure_val + 300))"
    log "  v4l2-ctl -d /dev/video0 --set-ctrl=analogue_gain=$((analogue_gain_val + 200))"
}

# Show current image quality settings
show_image_settings() {
    log "Current image quality settings:"
    echo ""
    
    # Get all control values
    v4l2-ctl -d /dev/video0 --get-ctrl=auto_exposure,exposure,analogue_gain,white_balance_automatic,gain_automatic 2>&1 || true
}

# Configure the OV5647 sensor
configure_sensor() {
    local resolution=$1
    local fps=$2
    local dry_run=$3
    
    parse_resolution "$resolution"
    
    local max_fps=$(get_max_framerate "$resolution")
    
    if [ -z "$fps" ]; then
        fps=$max_fps
        log "Auto-setting framerate to $fps fps for $resolution"
    fi
    
    log "Configuring OV5647 sensor for ${WIDTH}x${HEIGHT} @ ${fps}fps..."
    
    if [ "$dry_run" = "true" ]; then
        warn "DRY RUN MODE - No changes will be made"
        echo ""
        echo "Would execute:"
        echo "  1. Stop rkaiq_3A service"
        echo "  2. Set sensor resolution to ${WIDTH}x${HEIGHT}"
        echo "  3. Update media controller pipeline"
        echo "  4. Set ISP output format to ${WIDTH}x${HEIGHT}"
        echo "  5. Configure image quality (exposure, gain, etc.)"
        echo "  6. Test configuration"
        echo ""
        return 0
    fi
    
    # Stop rkaiq to avoid conflicts
    stop_rkaiq
    
    # Wait a moment for device to be free
    sleep 2
    
    # Find sensor device
    local sensor_dev=$(find_sensor_device)
    if [ -z "$sensor_dev" ]; then
        error "Cannot proceed without sensor device"
        exit 1
    fi
    
    # Configure sensor resolution
    log "Setting sensor resolution to ${WIDTH}x${HEIGHT}..."
    v4l2-ctl -d "$sensor_dev" --set-subdev-fmt "width=$WIDTH,height=$HEIGHT" 2>&1 || {
        error "Failed to set sensor resolution"
        exit 1
    }
    
    # Configure sensor frame rate using v4l2-ctl subdev FPS control
    log "Setting sensor frame rate to ${fps}fps..."
    if [ "$fps" -gt 0 ]; then
        # Use the dedicated subdev frame interval ioctl
        local fps_result=$(v4l2-ctl -d "$sensor_dev" --set-subdev-fps pad=0,fps=$fps 2>&1) || {
            warn "Could not set frame rate on subdev (driver may not support this)"
            debug "FPS setting result: $fps_result"
        }
    fi
    
    # Verify actual sensor format (may differ due to native sensor modes)
    log "Verifying sensor configuration..."
    
    # Get current sensor format
    local sensor_info=$(v4l2-ctl -d "$sensor_dev" --get-subdev-fmt 2>&1)
    debug "Sensor format:\n$sensor_info"
    
    local actual_w=$(echo "$sensor_info" | grep "Width/Height" | sed 's/.*:\s*\([0-9]*\)\/\([0-9]*\).*/\1/')
    local actual_h=$(echo "$sensor_info" | grep "Width/Height" | sed 's/.*:\s*\([0-9]*\)\/\([0-9]*\).*/\2/')
    
    if [ -n "$actual_w" ] && [ -n "$actual_h" ]; then
        if [ "${actual_w}" != "$WIDTH" ] || [ "${actual_h}" != "$HEIGHT" ]; then
            warn "Sensor native mode: ${actual_w}x${actual_h}, ISP will scale to ${WIDTH}x${HEIGHT}"
        else
            log "Sensor resolution confirmed: ${WIDTH}x${HEIGHT}"
        fi
    fi
    
    # Get and verify frame rate
    local fps_info=$(v4l2-ctl -d "$sensor_dev" --get-subdev-fps 2>&1)
    if echo "$fps_info" | grep -q "Frames per second"; then
        local actual_fps=$(echo "$fps_info" | grep "Frames per second" | sed 's/.*(\([0-9]*\).*/\1/' | awk '{printf "%d", $1/10000}')
        debug "Frame rate info: $fps_info"
        
        if [ "${actual_fps}" != "$fps" ]; then
            warn "Actual frame rate: ${actual_fps}fps (requested ${fps}fps, limited by sensor/MIPI)"
            fps=$actual_fps
        else
            log "Frame rate confirmed: ${fps}fps"
        fi
    fi
    
    # Configure image quality (pass resolution for tuned parameters)
    configure_image_quality "${WIDTH}x${HEIGHT}"
    
    # Verify configuration
    log "Verifying configuration..."
    local current_fmt=$(v4l2-ctl -d "$sensor_dev" --get-subdev-fmt 2>&1 || true)
    log "Current sensor format:\n$current_fmt"
    
    # Update media controller configuration
    log "Updating media controller pipeline..."
    media-ctl -d /dev/media0 --set-v4l2 "\"m00_b_ov5647 2-0036\":0 [fmt:SGBRG10_1X10/${WIDTH}x${HEIGHT}]" 2>&1 || \
    warn "Could not update media controller (this may be normal)"
    
    # Set ISP output format
    log "Setting ISP output format..."
    v4l2-ctl -d /dev/video0 --set-fmt-video="width=$WIDTH,height=$HEIGHT,pixelformat=NV12" 2>&1 || \
    warn "Could not set ISP output format"
    
    # Test the configuration
    log "Testing configuration (capturing 30 frames)..."
    timeout 10 v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=30 --stream-to=/dev/null 2>&1 | grep -E "fps|captured" || \
    warn "Could not test capture (this may be normal if no display)"
    
    log "Configuration complete!"
    log "Resolution: ${WIDTH}x${HEIGHT}"
    log "Target FPS: $fps"
    log "You can now use the camera with: ffmpeg -f v4l2 -video_size ${WIDTH}x${HEIGHT} -i /dev/video0 ..."
    
    # Optionally restart rkaiq
    # start_rkaiq
}

# Check current configuration
check_config() {
    print_banner
    echo ""
    log "Checking current OV5647 configuration..."
    echo ""
    
    # Find the OV5647 sensor sub-device
    local sensor_dev=$(find_sensor_device)
    if [ -z "$sensor_dev" ]; then
        error "Could not find sensor device"
        return 1
    fi
    
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ Sensor Information                                        │"
    echo "└─────────────────────────────────────────────────────────────┘"
    log "Found OV5647 sensor at: $sensor_dev"
    
    # Get current format
    echo ""
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ Current Sensor Configuration                               │"
    echo "└─────────────────────────────────────────────────────────────┘"
    v4l2-ctl -d "$sensor_dev" --get-subdev-fmt 2>&1 || true
    
    # Get media controller config
    echo ""
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ Media Controller Pipeline                                  │"
    echo "└─────────────────────────────────────────────────────────────┘"
    media-ctl -d /dev/media0 --get-v4l2 '"m00_b_ov5647 2-0036":0' 2>&1 || \
    media-ctl -d /dev/media0 -p 2>&1 | grep -A2 "m00_b_ov5647" || true
    
    # Get ISP output format
    echo ""
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ ISP Output Format (video0)                                │"
    echo "└─────────────────────────────────────────────────────────────┘"
    v4l2-ctl -d /dev/video0 --get-fmt-video 2>&1 || true
    
    # Get current image quality settings
    echo ""
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ Current Image Quality Settings                           │"
    echo "└─────────────────────────────────────────────────────────────┘"
    v4l2-ctl -d /dev/video0 --get-ctrl=auto_exposure,exposure,analogue_gain,white_balance_automatic,gain_automatic 2>&1 || true
    
    # Test actual frame rate
    echo ""
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ Testing Actual Capture Frame Rate (5 seconds)              │"
    echo "└─────────────────────────────────────────────────────────────┘"
    timeout 5 v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=50 --stream-to=/dev/null 2>&1 | grep fps || true
    
    echo ""
    log "Configuration check complete"
}

# Test actual capture frame rate
test_framerate() {
    print_banner
    echo ""
    log "Testing actual capture frame rate..."
    echo ""
    
    # Get current resolution
    local sensor_dev=$(find_sensor_device)
    if [ -z "$sensor_dev" ]; then
        error "Could not find sensor device"
        return 1
    fi
    
    local width=$(v4l2-ctl -d "$sensor_dev" --get-subdev-fmt 2>&1 | grep "Width/Height" | awk '{print $2}' | cut -d'/' -f1)
    local height=$(v4l2-ctl -d "$sensor_dev" --get-subdev-fmt 2>&1 | grep "Width/Height" | awk '{print $2}' | cut -d'/' -f2)
    
    log "Current resolution: ${width}x${height}"
    log "Testing capture for 10 seconds..."
    echo ""
    
    timeout 10 v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=100 --stream-to=/dev/null 2>&1 | grep -E "fps|frames" || {
        warn "Could not test capture"
        return 1
    }
    
    echo ""
    log "Test complete"
}

# ============================================================================
# Service Management Functions
# ============================================================================

install_service() {
    log "Installing OV5647 configuration service..."
    
    if [ "$EUID" -ne 0 ]; then
        error "Please run with sudo/root to install service"
        exit 1
    fi
    
    # Create config file if not exists
    if [ ! -f "$CONFIG_FILE" ]; then
        log "Creating config file: $CONFIG_FILE"
        cat > "$CONFIG_FILE" << EOF
# OV5647 Camera Configuration
RESOLUTION="$DEFAULT_RESOLUTION"
FRAMERATE="$DEFAULT_FRAMERATE"
VERBOSE="false"
EOF
    fi
    
    # Create systemd service
    log "Creating systemd service..."
    cat > /etc/systemd/system/ov5647-config.service << 'EOF'
[Unit]
Description=OV5647 Camera Configuration Service
After=multi-user.target
Wants=media-session.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/ov5647-config-tool.sh
RemainAfterExit=yes
StandardOutput=journal
StandardError=journal
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
    
    # Copy script to /usr/local/bin
    log "Installing script to /usr/local/bin/ov5647-config-tool.sh..."
    cp "$0" /usr/local/bin/ov5647-config-tool.sh
    chmod +x /usr/local/bin/ov5647-config-tool.sh
    
    # Reload and enable service
    log "Enabling service..."
    systemctl daemon-reload
    systemctl enable ov5647-config.service
    
    log "Installation complete!"
    log "Service installed and enabled"
    log "You can now:"
    log "  - Start service: systemctl start ov5647-config.service"
    log "  - Check status: systemctl status ov5647-config.service"
    log "  - View logs: journalctl -u ov5647-config.service -f"
}

uninstall_service() {
    log "Uninstalling OV5647 configuration service..."
    
    if [ "$EUID" -ne 0 ]; then
        error "Please run with sudo/root to uninstall service"
        exit 1
    fi
    
    # Stop and disable service
    systemctl stop ov5647-config.service 2>/dev/null || true
    systemctl disable ov5647-config.service 2>/dev/null || true
    
    # Remove files
    rm -f /etc/systemd/system/ov5647-config.service
    rm -f /usr/local/bin/ov5647-config-tool.sh
    rm -f "$CONFIG_FILE"
    
    # Reload systemd
    systemctl daemon-reload
    
    log "Uninstallation complete!"
}

show_service_status() {
    echo ""
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ OV5647 Configuration Service Status                        │"
    echo "└─────────────────────────────────────────────────────────────┘"
    echo ""
    
    if systemctl is-active --quiet ov5647-config.service 2>/dev/null; then
        echo -e "${GREEN}Status: RUNNING${NC}"
    else
        echo -e "${YELLOW}Status: STOPPED${NC}"
    fi
    
    if systemctl is-enabled --quiet ov5647-config.service 2>/dev/null; then
        echo -e "${GREEN}Auto-start: ENABLED${NC}"
    else
        echo -e "${YELLOW}Auto-start: DISABLED${NC}"
    fi
    
    echo ""
    systemctl status ov5647-config.service --no-pager 2>/dev/null || echo "Service not installed"
}

start_service() {
    log "Starting OV5647 configuration service..."
    systemctl start ov5647-config.service
    sleep 2
    show_service_status
}

stop_service() {
    log "Stopping OV5647 configuration service..."
    systemctl stop ov5647-config.service
    sleep 2
    show_service_status
}

restart_service() {
    log "Restarting OV5647 configuration service..."
    systemctl restart ov5647-config.service
    sleep 2
    show_service_status
}

# ============================================================================
# Main Function
# ============================================================================

main() {
    local resolution="$DEFAULT_RESOLUTION"
    local fps=""
    local list=false
    local check=false
    local test=false
    local install=false
    local uninstall=false
    local quality=false
    local show_settings=false
    local show_status=false
    local start_svc=false
    local stop_svc=false
    local restart_svc=false
    local dry_run=false
    
    # Source configuration file if exists
    if [ -f "$CONFIG_FILE" ]; then
        debug "Loading configuration from $CONFIG_FILE"
        source "$CONFIG_FILE"
        # Use config file values as defaults
        if [ -n "$RESOLUTION" ]; then
            resolution="$RESOLUTION"
        fi
        if [ -n "$FRAMERATE" ]; then
            fps="$FRAMERATE"
        fi
    fi
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -r|--resolution)
                resolution="$2"
                shift 2
                ;;
            -f|--framerate)
                fps="$2"
                shift 2
                ;;
            -l|--list)
                list=true
                shift
                ;;
            -c|--check)
                check=true
                shift
                ;;
            -t|--test)
                test=true
                shift
                ;;
            -q|--quality)
                quality=true
                shift
                ;;
            -I|--image-settings)
                show_settings=true
                shift
                ;;
            -i|--install)
                install=true
                shift
                ;;
            -u|--uninstall)
                uninstall=true
                shift
                ;;
            -s|--status)
                show_status=true
                shift
                ;;
            -S|--start)
                start_svc=true
                shift
                ;;
            -T|--stop)
                stop_svc=true
                shift
                ;;
            -R|--restart)
                restart_svc=true
                shift
                ;;
            -v|--verbose)
                VERBOSE="true"
                shift
                ;;
            --dry-run)
                dry_run=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # Execute requested action
    if [ "$list" = true ]; then
        list_modes
        exit 0
    fi
    
    if [ "$check" = true ]; then
        check_config
        exit 0
    fi
    
    if [ "$test" = true ]; then
        test_framerate
        exit 0
    fi
    
    if [ "$quality" = true ]; then
        configure_image_quality
        exit 0
    fi
    
    if [ "$show_settings" = true ]; then
        show_image_settings
        exit 0
    fi
    
    if [ "$install" = true ]; then
        install_service
        exit 0
    fi
    
    if [ "$uninstall" = true ]; then
        uninstall_service
        exit 0
    fi
    
    if [ "$show_status" = true ]; then
        show_service_status
        exit 0
    fi
    
    if [ "$start_svc" = true ]; then
        start_service
        exit 0
    fi
    
    if [ "$stop_svc" = true ]; then
        stop_service
        exit 0
    fi
    
    if [ "$restart_svc" = true ]; then
        restart_service
        exit 0
    fi
    
    # Default action: configure sensor
    print_banner
    echo ""
    
    # Wait for camera to be ready
    wait_for_camera
    
    # Configure sensor
    configure_sensor "$resolution" "$fps" "$dry_run"
}

# ============================================================================
# Script Entry Point
# ============================================================================

# Initialize verbose mode
VERBOSE="${VERBOSE:-false}"

# Create log file directory
mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null || true
touch "$LOG_FILE" 2>/dev/null || true

# Run main function
main "$@"
