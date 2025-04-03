#!/usr/bin/env bash

set -ve
mkdir -p build-release && cd build-release
${DEVKITPRO}/portlibs/wii/bin/powerpc-eabi-cmake -DCMAKE_BUILD_TYPE=Release ..
make -j`nproc`
