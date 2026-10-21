#!/bin/sh
# One-step build for macOS and Linux. Run from this folder: ./build.sh
set -e
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
echo
echo "Built. Start the game with:  ./build/living_marble"
