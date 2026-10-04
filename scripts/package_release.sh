#!/bin/bash
#
# package_release.sh — Build and package 0x808 for release.
#
# Usage:
#   ./scripts/package_release.sh [version]
#
# Examples:
#   ./scripts/package_release.sh 1.0.0
#   ./scripts/package_release.sh          # defaults to "dev"
#
# Builds:
#   - Linux (native): 0x808 + 0x808_gtk + plugins (tarball) and AppImage
#   - Windows (cross-compile): 0x808.exe + plugins (zip) and NSIS installer
#
# AppImage needs appimagetool (on PATH, ~/tools/appimagetool, or
# APPIMAGETOOL=/path). The installer needs makensis and a numeric version.
#
# Outputs to release/ directory.
#

set -eo pipefail  # a failed build must stop the script, even piped through tail

VERSION="${1:-dev}"
RELEASE_DIR="release/0x808-${VERSION}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "=== 0x808 Release Packaging v${VERSION} ==="

# ── Clean release dir ──
rm -rf "release"
mkdir -p "${RELEASE_DIR}-linux-x64"
mkdir -p "${RELEASE_DIR}-windows-x64"

# ── Build Linux ──
echo ""
echo "--- Building Linux (native) ---"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_GTK=ON -DENABLE_MP3=ON 2>&1 | tail -3
cmake --build build -j$(nproc) 2>&1 | tail -5

cp build/0x808          "${RELEASE_DIR}-linux-x64/" 2>/dev/null || echo "  (no ImGui binary)"
cp build/0x808_gtk      "${RELEASE_DIR}-linux-x64/" 2>/dev/null || echo "  (no GTK binary)"
cp build/0x808.clap     "${RELEASE_DIR}-linux-x64/" 2>/dev/null || echo "  (no CLAP plugin)"
cp -r build/0x808.vst3  "${RELEASE_DIR}-linux-x64/" 2>/dev/null || echo "  (no VST3 plugin)"
cp -r samples           "${RELEASE_DIR}-linux-x64/"
cp -r themes            "${RELEASE_DIR}-linux-x64/"
cp README.md LICENSE     "${RELEASE_DIR}-linux-x64/"

echo "  Creating tarball..."
cd release
tar czf "0x808-${VERSION}-linux-x64.tar.gz" "0x808-${VERSION}-linux-x64"
cd "$PROJECT_DIR"
echo "  -> release/0x808-${VERSION}-linux-x64.tar.gz"

# ── AppImage (assembled fresh from the current tree every time) ──
APPIMAGETOOL="${APPIMAGETOOL:-$(command -v appimagetool || echo "$HOME/tools/appimagetool")}"
if [ -x "$APPIMAGETOOL" ] && [ -f build/0x808 ]; then
    echo "  Creating AppImage..."
    APPDIR="release/0x808.AppDir"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/256x256/apps"
    cp resources/appimage/AppRun       "$APPDIR/"
    cp resources/appimage/0x808.desktop "$APPDIR/"
    cp resources/appimage/0x808.desktop "$APPDIR/usr/share/applications/"
    cp resources/0x808.png             "$APPDIR/"
    cp resources/0x808.png             "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
    ln -sf 0x808.png                   "$APPDIR/.DirIcon"
    cp build/0x808                     "$APPDIR/usr/bin/"
    cp -r samples themes               "$APPDIR/usr/bin/"
    ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "release/0x808-${VERSION}-x86_64.AppImage" > /dev/null
    echo "  -> release/0x808-${VERSION}-x86_64.AppImage"
else
    echo "  (skipping AppImage: appimagetool not found — set APPIMAGETOOL=/path)"
fi

# ── Build Windows (cross-compile) ──
echo ""
echo "--- Building Windows (MinGW cross-compile) ---"
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DENABLE_MP3=ON 2>&1 | tail -3
cmake --build build_win -j$(nproc) 2>&1 | tail -5

cp build_win/0x808.exe  "${RELEASE_DIR}-windows-x64/" 2>/dev/null || echo "  (no exe)"
cp build_win/0x808.clap "${RELEASE_DIR}-windows-x64/" 2>/dev/null || echo "  (no CLAP plugin)"
cp -r build_win/0x808.vst3 "${RELEASE_DIR}-windows-x64/" 2>/dev/null || echo "  (no VST3 plugin)"
cp -r samples           "${RELEASE_DIR}-windows-x64/"
cp -r themes            "${RELEASE_DIR}-windows-x64/"
cp README.md LICENSE     "${RELEASE_DIR}-windows-x64/"

echo "  Creating archive..."
cd release
if command -v zip &>/dev/null; then
    zip -r "0x808-${VERSION}-windows-x64.zip" "0x808-${VERSION}-windows-x64" -q
    echo "  -> release/0x808-${VERSION}-windows-x64.zip"
else
    tar czf "0x808-${VERSION}-windows-x64.tar.gz" "0x808-${VERSION}-windows-x64"
    echo "  -> release/0x808-${VERSION}-windows-x64.tar.gz (zip not available, used tar)"
fi
cd "$PROJECT_DIR"

# ── Windows installer (NSIS) ──
if command -v makensis &>/dev/null && [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "  Creating installer..."
    makensis -V2 -DVER="${VERSION}" scripts/0x808_installer.nsi
    echo "  -> release/0x808-${VERSION}-windows-x64-setup.exe"
else
    echo "  (skipping installer: needs makensis and a numeric version)"
fi

# ── Summary ──
echo ""
echo "=== Release artifacts ==="
ls -lh release/*.tar.gz release/*.zip release/*.exe release/*.AppImage 2>/dev/null
echo ""
echo "To create a GitHub release:"
echo "  git tag v${VERSION}"
echo "  git push origin v${VERSION}"
echo "  gh release create v${VERSION} release/*.tar.gz release/*.zip release/*.exe release/*.AppImage --title 'v${VERSION}' --generate-notes"
