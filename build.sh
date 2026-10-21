#!/bin/sh
# One-step build. Run from this folder: ./build.sh
#   macOS: the native app (Swift + Metal) -> macos/build/Living Marble.app
#   Linux: the C++/OpenGL version         -> build/living_marble
set -e
if [ "$(uname -s)" = "Darwin" ]; then
  exec ./macos/build-app.sh "$@"
fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
echo
echo "Built. Start the game with:  ./build/living_marble"
