#!/bin/sh
# Builds the Mac installer: a disk image with "Living Marble" and an
# Applications shortcut to drag it onto. The app is a universal build signed
# with your Developer ID (hardened runtime), so it opens on other Macs.
#   ./build-dmg.sh                           sign with the first Developer ID Application identity
#   SIGN_ID=<sha1 or name> ./build-dmg.sh    pick the identity
#   NOTARY_PROFILE=<name> ./build-dmg.sh     also notarize and staple (a profile saved
#                                            with: xcrun notarytool store-credentials <name>)
set -e
cd "$(dirname "$0")"

if [ -z "$SIGN_ID" ]; then
  SIGN_ID=$(security find-identity -v -p codesigning | awk '/"Developer ID Application/ { print $2; exit }')
fi
if [ -z "$SIGN_ID" ]; then
  echo "No Developer ID Application certificate in the keychain (or set SIGN_ID)." >&2
  exit 1
fi

./build-app.sh --universal

APP="build/Living Marble.app"
VERSION=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' Info.plist)
DMG="build/LivingMarble-$VERSION.dmg"

codesign --force --options runtime --timestamp --sign "$SIGN_ID" "$APP"
codesign --verify --strict "$APP"

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
