#!/bin/bash
# Build script for data_track Docker image
docker rm -f webrtc_data_track
docker rmi webrtc/data-track:latest
docker build -t webrtc/data-track:latest .
