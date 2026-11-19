#!/bin/sh
# Build a project-local LGPL FFmpeg and a Qt Multimedia that uses it; leave Homebrew's Qt and FFmpeg untouched.
# FFmpeg is configured without GPL or nonfree components (no x264/x265), so the redistributed libraries stay
# LGPL 2.1+. H.264/HEVC/AAC encoding comes from Apple's VideoToolbox and AudioToolbox.
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Darwin ]; then
    echo "Run this helper on macOS." >&2
    exit 1
fi
QT_MACOS_ROOT="${QT_MACOS_ROOT:-$(brew --prefix qt)}"
qt_version="$("$QT_MACOS_ROOT/bin/qtpaths" --qt-version)"
if [ "$qt_version" != 6.12.0 ]; then
    echo "This checksum-pinned helper requires Qt 6.12.0; found $qt_version." >&2
    exit 1
fi
ffmpeg_version=9.0.2
ffmpeg_sha256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
ffmpeg_root="$PWD/.tools/ffmpeg-lgpl"
mkdir -p "$ffmpeg_root"
if [ ! -f "$ffmpeg_root/source.tar.xz" ]; then
    curl -fL --retry 2 "https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz" -o "$ffmpeg_root/source.tar.xz"
fi
printf '%s  %s\n' "$ffmpeg_sha256" "$ffmpeg_root/source.tar.xz" | shasum -a 256 -c -
ffmpeg_src="$ffmpeg_root/ffmpeg-$ffmpeg_version"
if [ ! -d "$ffmpeg_src" ]; then
    tar -xf "$ffmpeg_root/source.tar.xz" -C "$ffmpeg_root"
fi
ffmpeg_stamp="$ffmpeg_version lgpl-shared-videotoolbox"
if [ "$(cat "$ffmpeg_root/install/.stamp" 2>/dev/null || true)" != "$ffmpeg_stamp" ]; then
    rm -rf "$ffmpeg_root/install" "$ffmpeg_root/build"
    mkdir -p "$ffmpeg_root/build"
    (cd "$ffmpeg_root/build" && "$ffmpeg_src/configure" --prefix="$ffmpeg_root/install" \
        --enable-shared --disable-static --enable-pic \
        --disable-gpl --disable-version3 --disable-nonfree --disable-autodetect \
        --disable-programs --disable-doc --disable-debug \
        --enable-videotoolbox --enable-audiotoolbox --enable-zlib)
    make -C "$ffmpeg_root/build" -j"${JOBS:-8}"
    make -C "$ffmpeg_root/build" install
    echo "$ffmpeg_stamp" > "$ffmpeg_root/install/.stamp"
fi
# Refuse anything that is not LGPL, whatever the configure flags were.
if ! strings -a "$ffmpeg_root"/install/lib/libavutil.*.dylib | grep -q "libavutil license: LGPL"; then
    echo "The built FFmpeg is not LGPL; refusing to continue." >&2
    exit 1
fi
root="$PWD/.tools/qt-multimedia-ffmpeg"
mkdir -p "$root"
if [ ! -f "$root/source.tar.xz" ]; then
    curl -fL --retry 2 "https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtmultimedia-everywhere-src-6.12.0.tar.xz" -o "$root/source.tar.xz"
fi
printf '%s  %s\n' 3143f53b64257ba2a0685c940f5dce476c75871ff86b516de45cfd54ea93a62d "$root/source.tar.xz" | shasum -a 256 -c -
if [ ! -d "$root/qtmultimedia-everywhere-src-6.12.0" ]; then
    tar -xf "$root/source.tar.xz" -C "$root"
fi
# A build tree configured against another FFmpeg (e.g. Homebrew's GPL build) must not be reused.
if [ "$(cat "$root/build/.ffmpeg-root" 2>/dev/null || true)" != "$ffmpeg_root/install" ]; then
    rm -rf "$root/build" "$root/install"
fi
cmake -S "$root/qtmultimedia-everywhere-src-6.12.0" -B "$root/build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$QT_MACOS_ROOT" -DCMAKE_INSTALL_PREFIX="$root/install" \
    -DCMAKE_BUILD_TYPE=Release -DQT_FEATURE_ffmpeg=ON -DFFMPEG_DIR="$ffmpeg_root/install" \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
    -DQT_NO_APPLE_SDK_AND_XCODE_CHECK=ON # only the Command Line Tools are required, not Xcode
echo "$ffmpeg_root/install" > "$root/build/.ffmpeg-root"
cmake --build "$root/build" --parallel "${JOBS:-8}"
cmake --install "$root/build"
echo "Installed LGPL FFmpeg: $ffmpeg_root/install"
echo "Installed local Qt Multimedia: $root/install"
echo "Run ./build-macos-arm64.sh to build and test using this installation."
