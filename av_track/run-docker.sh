#!/bin/bash
# Run script for av_track container

# Default values
IMAGE_NAME="${IMAGE_NAME:-webrtc/av-track:latest}"
CONTAINER_NAME="${CONTAINER_NAME:-webrtc_av_track}"
VIDEO_DEVICE="${VIDEO_DEVICE:-/dev/video1}"
CLIENT_ID="${CLIENT_ID:-cam_dYFh3H3kf}"
NETWORK_MODE="${NETWORK_MODE:-host}"

# Optional: add additional device mappings
DEVICES="--device=$VIDEO_DEVICE"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --device)
            DEVICES="$DEVICES --device=$2"
            shift 2
            ;;
        --name)
            CONTAINER_NAME="$2"
            shift 2
            ;;
        --client-id)
            CLIENT_ID="$2"
            shift 2
            ;;
        --network)
            NETWORK_MODE="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--device /dev/xxx] [--name container_name] [--client-id id] [--network mode]"
            exit 1
            ;;
    esac
done

# Run container
docker run -d \
  --name $CONTAINER_NAME \
  --restart unless-stopped \
  $DEVICES \
  -e VIDEO_DEVICE=$VIDEO_DEVICE \
  -e CLIENT_ID=$CLIENT_ID \
  --network $NETWORK_MODE \
  $IMAGE_NAME

echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
