#!/bin/bash
dos2unix *
cd cmake-build-debug-zero2
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=mem_leak.log  ./webrtc_publisher -s stun.l.google.com -t 19302 -w fy403.cn -x 8000 -c usbcam -i /dev/video1 -F 20 -R 1024x768 --debug