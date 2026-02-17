#!/bin/bash
# Build script for av_track Docker image
docker rm -f webrtc_av_track
docker rmi webrtc/av-track:latest
docker build -t webrtc/av-track:latest .
