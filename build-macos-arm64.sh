#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Darwin ]; then
    echo "Run this helper on an Apple Silicon Mac (Xcode command-line tools and Qt 6 required)." >&2
    exit 1
fi
if [ -z "${QT_MACOS_ROOT:-}" ]; then
    if command -v brew >/dev/null 2>&1; then
        QT_MACOS_ROOT="$(brew --prefix qt)"
    else
        echo "Set QT_MACOS_ROOT to your Qt 6 macOS kit, including Shader Tools and private headers." >&2
        exit 1
    fi
fi
export QT_MACOS_ROOT
export PATH="$QT_MACOS_ROOT/bin:$PATH"
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64
dist_dir="$PWD/dist/macos-arm64"
rm -rf "$dist_dir"
cmake --install build-macos-arm64 --prefix "$dist_dir"
# Local ad-hoc signature after Qt framework/plugin deployment. Distribution signing is separate.
/usr/bin/codesign --force --deep --sign - "$dist_dir/sameboy-qt.app"
/usr/bin/lipo -verify_arch arm64 "$dist_dir/sameboy-qt.app/Contents/MacOS/sameboy-qt"
echo "Built: $dist_dir/sameboy-qt.app"
