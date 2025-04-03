#!/usr/bin/env bash

set -ve
mkdir -p build && cd build
${DEVKITPRO}/portlibs/wii/bin/powerpc-eabi-cmake ..
make -j`nproc`
