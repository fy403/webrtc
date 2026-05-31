#!/bin/bash
# Build script for cmd_track Docker image
docker rm -f webrtc_cmd_track
docker rmi webrtc/cmd-track:latest
docker build -t webrtc/cmd-track:latest .
