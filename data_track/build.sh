#!/bin/bash
dos2unix *
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake ..
make -j3