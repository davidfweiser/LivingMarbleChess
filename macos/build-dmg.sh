#!/bin/sh
# Builds the Mac installer: a disk image with "Living Marble" and an
# Applications shortcut to drag it onto.
#   ./build-dmg.sh                           universal build signed with your Developer ID
#                                            (hardened runtime), so it opens on other Macs
#   ./build-dmg.sh "path/to/Living Marble.app"   pack an app exported from Xcode (Distribute
#                                            App › Direct Distribution, already notarized) as it is
#   SIGN_ID=<sha1 or name> ./build-dmg.sh    pick the identity (default: the first Developer ID
#                                            Application one in the keychain)
#   NOTARY_PROFILE=<name> ./build-dmg.sh     also notarize and staple the disk image (a profile
#                                            saved with: xcrun notarytool store-credentials <name>)
set -e
EXPORTED=""
if [ -n "$1" ]; then
  EXPORTED="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
fi
cd "$(dirname "$0")"

if [ -z "$SIGN_ID" ]; then
  SIGN_ID=$(security find-identity -v -p codesigning | awk '/"Developer ID Application/ { print $2; exit }')
fi
if [ -z "$SIGN_ID" ]; then
  echo "No Developer ID Application certificate in the keychain (or set SIGN_ID)." >&2
  exit 1
fi

if [ -n "$EXPORTED" ]; then
  APP="$EXPORTED"
else
  ./build-app.sh --universal
  APP="build/Living Marble.app"
  codesign --force --options runtime --timestamp --sign "$SIGN_ID" "$APP"
fi
codesign --verify --strict "$APP"

VERSION=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")
DMG="build/LivingMarble-$VERSION.dmg"

STAGE="build/dmg"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
ditto "$APP" "$STAGE/Living Marble.app"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "Living Marble" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null
rm -rf "$STAGE"
codesign --force --timestamp --sign "$SIGN_ID" "$DMG"

if [ -n "$NOTARY_PROFILE" ]; then
  xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
fi

echo
echo "Built: macos/$DMG"
