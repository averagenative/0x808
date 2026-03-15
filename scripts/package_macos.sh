#!/bin/bash
#
# package_macos.sh — Build and package 0x808 for macOS.
#
# Run this on a Mac with Homebrew, SDL2, and CMake installed:
#   brew install sdl2 cmake
#   ./scripts/package_macos.sh [version]
#
# Outputs:
#   release/0x808-{version}-macos-x64.dmg
#   release/0x808-{version}-macos-x64.zip
#

set -e

VERSION="${1:-dev}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
APP_NAME="0x808"
RELEASE_DIR="release"
APP_DIR="${RELEASE_DIR}/${APP_NAME}.app"

cd "$PROJECT_DIR"

echo "=== 0x808 macOS Release Packaging v${VERSION} ==="

# ── Build ──
echo "--- Building macOS ---"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_MP3=ON
cmake --build build -j$(sysctl -n hw.ncpu)

# ── Create .app bundle ──
echo "--- Creating .app bundle ---"
rm -rf "${RELEASE_DIR}"
mkdir -p "${APP_DIR}/Contents/MacOS"
mkdir -p "${APP_DIR}/Contents/Resources"

# Binary
cp build/0x808 "${APP_DIR}/Contents/MacOS/${APP_NAME}"

# Resources
cp -r samples "${APP_DIR}/Contents/Resources/"
cp -r themes  "${APP_DIR}/Contents/Resources/"

# Icon
if [ -f resources/0x808.icns ]; then
    cp resources/0x808.icns "${APP_DIR}/Contents/Resources/"
    echo "  -> App icon: 0x808.icns"
fi

# Info.plist
cat > "${APP_DIR}/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>com.dcmichael.0x808</string>
    <key>CFBundleName</key>
    <string>0x808</string>
    <key>CFBundleIconFile</key>
    <string>0x808</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
</dict>
</plist>
PLIST

echo "  -> ${APP_DIR}"

# ── Create .dmg ──
echo "--- Creating .dmg ---"
DMG_NAME="0x808-${VERSION}-macos-x64.dmg"

# Stage for DMG
DMG_STAGE="${RELEASE_DIR}/dmg_stage"
mkdir -p "${DMG_STAGE}"
cp -r "${APP_DIR}" "${DMG_STAGE}/"
cp README.md LICENSE "${DMG_STAGE}/"
ln -s /Applications "${DMG_STAGE}/Applications"

hdiutil create -volname "0x808 v${VERSION}" \
    -srcfolder "${DMG_STAGE}" \
    -ov -format UDZO \
    "${RELEASE_DIR}/${DMG_NAME}" 2>/dev/null || {
    echo "  hdiutil failed — creating zip instead"
    cd "${RELEASE_DIR}"
    zip -r "0x808-${VERSION}-macos-x64.zip" "${APP_NAME}.app" -q
    cd "$PROJECT_DIR"
}

rm -rf "${DMG_STAGE}"

# ── Prepare plugins ──
echo "--- Preparing plugins ---"
PLUGIN_DIR="${RELEASE_DIR}/Plugins"
mkdir -p "${PLUGIN_DIR}"

# CLAP — single file
if [ -f build/0x808.clap ]; then
    cp build/0x808.clap "${PLUGIN_DIR}/"
    echo "  -> CLAP: ${PLUGIN_DIR}/0x808.clap"
fi

# VST3 — needs macOS bundle structure
if [ -f build/0x808.so ]; then
    VST3_BUNDLE="${PLUGIN_DIR}/0x808.vst3"
    mkdir -p "${VST3_BUNDLE}/Contents/MacOS"
    cp build/0x808.so "${VST3_BUNDLE}/Contents/MacOS/0x808"
    cat > "${VST3_BUNDLE}/Contents/Info.plist" << VST3PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>0x808</string>
    <key>CFBundleIdentifier</key>
    <string>com.dcmichael.0x808.vst3</string>
    <key>CFBundleName</key>
    <string>0x808</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
</dict>
</plist>
VST3PLIST
    echo "  -> VST3: ${VST3_BUNDLE}"
fi

# Plugin install readme
cat > "${PLUGIN_DIR}/INSTALL_PLUGINS.txt" << 'PLUGINTXT'
0x808 — Plugin Installation (macOS)
====================================

Copy the plugins to these locations so your DAW can find them:

  VST3:  ~/Library/Audio/Plug-Ins/VST3/0x808.vst3
  CLAP:  ~/Library/Audio/Plug-Ins/CLAP/0x808.clap

Quick install (run in Terminal from this folder):

  mkdir -p ~/Library/Audio/Plug-Ins/VST3
  mkdir -p ~/Library/Audio/Plug-Ins/CLAP
  cp -r 0x808.vst3 ~/Library/Audio/Plug-Ins/VST3/
  cp 0x808.clap    ~/Library/Audio/Plug-Ins/CLAP/

Then restart your DAW and rescan plugins.
PLUGINTXT
echo "  -> ${PLUGIN_DIR}/INSTALL_PLUGINS.txt"

# ── Create zip (app + plugins) ──
echo "--- Creating zip ---"
cd "${RELEASE_DIR}"
zip -r "0x808-${VERSION}-macos-x64.zip" "${APP_NAME}.app" Plugins/ -q 2>/dev/null || true
cd "$PROJECT_DIR"

# ── Summary ──
echo ""
echo "=== macOS Release artifacts ==="
ls -lh "${RELEASE_DIR}"/*.dmg "${RELEASE_DIR}"/*.zip 2>/dev/null
echo ""
echo "To upload to GitHub release:"
echo "  gh release upload v${VERSION} ${RELEASE_DIR}/*.dmg ${RELEASE_DIR}/*.zip"
