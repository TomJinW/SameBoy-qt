#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Tests are compiled and run only with --test; by default the build is packaged directly.
run_tests=0
build_testing=OFF
for argument in "$@"; do
    case "$argument" in
        --test) run_tests=1; build_testing=ON ;;
        *) echo "Usage: ./build-macos-arm64.sh [--test]" >&2; exit 2 ;;
    esac
done
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
if [ ! -f "$QT_MACOS_ROOT/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Qt 6 was not found at: $QT_MACOS_ROOT" >&2
    echo "Install the build dependencies with: brew install qt cmake ninja" >&2
    echo "Or set QT_MACOS_ROOT to an existing Qt 6 macOS kit." >&2
    exit 1
fi
export QT_MACOS_ROOT
export PATH="$QT_MACOS_ROOT/bin:$PATH"
for tool in cmake ctest ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool. Install dependencies with: brew install qt cmake ninja" >&2
        exit 1
    fi
done
# The local Qt Multimedia from ./setup-macos-ffmpeg.sh supplies an LGPL FFmpeg (Homebrew's Qt has none);
# without it the app builds with Qt's native backend, which cannot record video.
media_root="${QT_MULTIMEDIA_ROOT:-$PWD/.tools/qt-multimedia-ffmpeg/install}"
if [ -f "$media_root/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake" ]; then
    export QT_MEDIA_BACKEND="${QT_MEDIA_BACKEND:-ffmpeg}"
    export QT_PLUGIN_PATH="$media_root/share/qt/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
    cmake --preset macos-arm64 -DBUILD_TESTING="$build_testing" -DQt6Multimedia_DIR="$media_root/lib/cmake/Qt6Multimedia"
else
    cmake --preset macos-arm64 -DBUILD_TESTING="$build_testing"
fi
cmake --build --preset macos-arm64
# A failing test is reported at the end; it must not block packaging.
tests_ok=1
if [ "$run_tests" = 1 ]; then ctest --preset macos-arm64 || tests_ok=0; fi
dist_dir="$PWD/dist/macos-arm64"
rm -rf "$dist_dir"
cmake --install build-macos-arm64 --prefix "$dist_dir"
# macdeployqt from the base Qt kit only discovers its own multimedia plugins.
# Include the locally built FFmpeg plugin and deploy its dependent libraries too.
media_plugin="$media_root/share/qt/plugins/multimedia/libffmpegmediaplugin.dylib"
if [ -f "$media_plugin" ]; then
    plugin_dir="$dist_dir/sameboy-qt.app/Contents/PlugIns/multimedia"
    mkdir -p "$plugin_dir"
    cp "$media_plugin" "$plugin_dir/"
    "$QT_MACOS_ROOT/bin/macdeployqt" "$dist_dir/sameboy-qt.app" \
        -executable="$plugin_dir/libffmpegmediaplugin.dylib"
    # Never ship a GPL or nonfree FFmpeg (e.g. libraries picked up from Homebrew).
    found_avutil=0
    for lib in "$dist_dir"/sameboy-qt.app/Contents/Frameworks/libavutil*.dylib; do
        [ -f "$lib" ] || continue
        found_avutil=1
        if ! strings -a "$lib" | grep -q "libavutil license: LGPL" || strings -a "$lib" | grep -q "nonfree and unredistributable"; then
            echo "Bundled FFmpeg is not LGPL ($lib). Run ./setup-macos-ffmpeg.sh to build the LGPL FFmpeg." >&2
            exit 1
        fi
    done
    if [ "$found_avutil" = 0 ]; then
        echo "No FFmpeg libraries were deployed with the FFmpeg media plugin." >&2
        exit 1
    fi
fi
# Local ad-hoc signature after Qt framework/plugin deployment. Distribution signing is separate.
/usr/bin/codesign --force --deep --sign - "$dist_dir/sameboy-qt.app"
/usr/bin/lipo -verify_arch arm64 "$dist_dir/sameboy-qt.app/Contents/MacOS/sameboy-qt"
echo "Built: $dist_dir/sameboy-qt.app"
if [ "$tests_ok" != 1 ]; then
    echo "WARNING: some tests FAILED, but the distribution was still produced. Review the test output above." >&2
fi
