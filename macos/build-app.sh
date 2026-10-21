#!/bin/sh
# Builds the native macOS app (Swift + Metal) as "Living Marble.app".
#   ./build-app.sh              release build for this Mac
#   ./build-app.sh --universal  Apple silicon + Intel
set -e
cd "$(dirname "$0")"

ARCHS=""
if [ "$1" = "--universal" ]; then
  ARCHS="--arch arm64 --arch x86_64"
fi

swift build -c release $ARCHS
BIN="$(swift build -c release $ARCHS --show-bin-path)/LivingMarble"

APP="build/Living Marble.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/LivingMarble"
cp Info.plist "$APP/Contents/Info.plist"
if [ -f AppIcon.icns ]; then cp AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"; fi
codesign --force --sign - "$APP" >/dev/null 2>&1

echo
echo "Built: macos/$APP"
echo "Start it with:  open \"macos/$APP\""
