#!/bin/bash
cd libdatachannel && git submodule update --init --recursive
mkdir build && cd build && cmake .. && make -j2 && sudo make install