#!/bin/bash
set -euo pipefail

TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$TASK_ROOT/build-macos-arm64"
PLUGIN_PATH="$BUILD_DIR/HarmonicShadow_artefacts/Release/AU/Harmonic Shadow.component"
INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/Components"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Build and installation require macOS with Xcode Command Line Tools."
  exit 1
fi

cmake -S "$TASK_ROOT" -B "$BUILD_DIR" \
  -DHARMONIC_SHADOW_BUILD_PLUGIN=ON \
  -DHARMONIC_SHADOW_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build "$BUILD_DIR" --target HarmonicShadow_AU HarmonicEngineTests --config Release
ctest --test-dir "$BUILD_DIR" --output-on-failure -C Release

if [[ ! -d "$PLUGIN_PATH" ]]; then
  echo "AU component was not found at: $PLUGIN_PATH"
  exit 2
fi

mkdir -p "$INSTALL_DIR"
rm -rf "$INSTALL_DIR/Harmonic Shadow.component"
cp -R "$PLUGIN_PATH" "$INSTALL_DIR/Harmonic Shadow.component"
codesign --force --deep --sign - "$INSTALL_DIR/Harmonic Shadow.component"
echo "Installed the local AU MIDI FX at: $INSTALL_DIR/Harmonic Shadow.component"
echo "Open Logic Pro Plug-in Manager and rescan Audio Units if it is not listed."
