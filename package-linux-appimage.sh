#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Linux ]; then
    echo "Run this helper on Linux, or use ./build-linux-apple-container.sh all --appimage." >&2
    exit 1
fi
case "$(uname -m)" in
    aarch64|arm64) arch=arm64; appimage_arch=aarch64 ;;
    x86_64|amd64) arch=x64; appimage_arch=x86_64 ;;
    *) echo "AppImage packaging supports arm64 and x64." >&2; exit 1 ;;
esac
build_dir="$PWD/build-linux-$arch"
dist_dir="$PWD/dist/linux-$arch"
tools_dir="$PWD/.tools/appimage-$arch"
mkdir -p "$tools_dir" "$dist_dir"
for tool in curl cmake; do command -v "$tool" >/dev/null 2>&1 || { echo "Missing tool: $tool" >&2; exit 1; }; done
# Official continuous builds are cached locally. Override tags for a pinned release.
fetch() {
    name="$1"; repo="$2"; tag="$3"
    if [ ! -s "$tools_dir/$name" ]; then
        curl -fL --retry 3 "https://github.com/$repo/releases/download/$tag/$name" -o "$tools_dir/$name.part"
        mv "$tools_dir/$name.part" "$tools_dir/$name"
    fi
    chmod +x "$tools_dir/$name"
}
fetch "linuxdeploy-$appimage_arch.AppImage" linuxdeploy/linuxdeploy "${LINUXDEPLOY_TAG:-continuous}"
fetch "linuxdeploy-plugin-qt-$appimage_arch.AppImage" linuxdeploy/linuxdeploy-plugin-qt "${LINUXDEPLOY_QT_TAG:-continuous}"
export APPIMAGE_EXTRACT_AND_RUN=1
linuxdeploy="$tools_dir/linuxdeploy-$appimage_arch.AppImage"
# Rosetta in Apple Container cannot exec the static AppImage runtime (Exec format error).
# Unpack the embedded squashfs instead and run the dynamically linked AppRun inside.
if ! "$linuxdeploy" --version >/dev/null 2>&1; then
    command -v unsquashfs >/dev/null 2>&1 || { echo "Cannot run linuxdeploy and unsquashfs is missing." >&2; exit 1; }
    unpack() {
        name="$1"; image="$tools_dir/$name.AppImage"; out="$tools_dir/$name.extracted"
        if [ ! -x "$out/AppRun" ]; then
            offset="$(python3 -c '
import struct, sys
h = open(sys.argv[1], "rb").read(64)
shoff, = struct.unpack_from("<Q", h, 0x28)
shentsize, shnum = struct.unpack_from("<HH", h, 0x3A)
print(shoff + shentsize * shnum)' "$image")"
            rm -rf "$out"
            unsquashfs -q -o "$offset" -d "$out" "$image" >/dev/null
        fi
    }
    unpack "linuxdeploy-$appimage_arch"
    unpack "linuxdeploy-plugin-qt-$appimage_arch"
    wrappers="$tools_dir/wrappers"
    mkdir -p "$wrappers"
    for pair in "linuxdeploy-$appimage_arch:linuxdeploy" "linuxdeploy-plugin-qt-$appimage_arch:linuxdeploy-plugin-qt"; do
        printf '#!/bin/sh\nexec "%s/%s.extracted/AppRun" "$@"\n' "$tools_dir" "${pair%%:*}" > "$wrappers/${pair##*:}"
        chmod +x "$wrappers/${pair##*:}"
    done
    export PATH="$wrappers:$PATH"
    linuxdeploy="$wrappers/linuxdeploy"
fi
if [ -z "${QMAKE:-}" ]; then
    if [ -n "${QT_LINUX_ROOT:-}" ] && [ -x "$QT_LINUX_ROOT/bin/qmake" ]; then
        QMAKE="$QT_LINUX_ROOT/bin/qmake"
    else
        QMAKE="$(command -v qmake6 || command -v qmake || true)"
    fi
fi
if [ -z "$QMAKE" ] || [ ! -x "$QMAKE" ]; then
    echo "Qt 6 qmake is missing. Install qmake6 or set QMAKE to your Qt kit's qmake." >&2
    exit 1
fi
case "$("$QMAKE" -query QT_VERSION)" in 6.*) ;; *) echo "AppImage packaging requires Qt 6 qmake." >&2; exit 1 ;; esac
export QMAKE
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export ARCH="$appimage_arch"
export VERSION="${APPIMAGE_VERSION:-0.1.0}"
output="$dist_dir/SameBoyQt-$VERSION-$appimage_arch.AppImage"
# Stage ELF patching/symlinks on Linux storage, not a macOS VirtioFS bind mount.
# Only the completed AppImage is copied back to the host.
stage="$(mktemp -d "${TMPDIR:-/tmp}/sameboy-appimage-$arch-XXXXXX")"
export LDAI_OUTPUT="$stage/$(basename "$output")"
export LDAI_VERSION="$VERSION"
appdir="$stage/AppDir"
trap 'rm -rf "$stage"' EXIT HUP INT TERM
DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr
set -- --appdir "$appdir" --desktop-file "$appdir/usr/share/applications/sameboy-qt.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/sameboy-qt.png"
# Include dynamically loaded backends and Wayland integration with their dependencies.
plugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
for dir in multimedia wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    for plugin in "$plugins/$dir/"*.so; do
        [ -f "$plugin" ] || continue
        mkdir -p "$appdir/usr/plugins/$dir"
        cp "$plugin" "$appdir/usr/plugins/$dir/"
        set -- "$@" --library "$appdir/usr/plugins/$dir/$(basename "$plugin")"
    done
done
"$linuxdeploy" "$@" --plugin qt
# linuxdeploy leaves no RUNPATH on the plugins copied in above, so they cannot find the
# bundled Qt libraries (e.g. the Wayland EGL integration fails to load). Fix before packing.
command -v patchelf >/dev/null 2>&1 || { echo "Missing tool: patchelf" >&2; exit 1; }
for dir in multimedia wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    for plugin in "$appdir/usr/plugins/$dir/"*.so; do
        [ -f "$plugin" ] && patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"
    done
done
# Never ship a GPL or nonfree FFmpeg (e.g. the distribution's libavcodec picked up instead of the LGPL build).
for lib in "$appdir"/usr/lib/libavutil.so*; do
    [ -f "$lib" ] || continue
    if ! strings -a "$lib" | grep -q "libavutil license: LGPL" || strings -a "$lib" | grep -q "nonfree and unredistributable"; then
        echo "Bundled FFmpeg is not LGPL ($lib). Build in the Apple Container image or put an LGPL FFmpeg first in LD_LIBRARY_PATH." >&2
        exit 1
    fi
done
# linuxdeploy can leave the distribution's GPL codec libraries behind even though the bundled FFmpeg does not
# use them. Remove them, and fail if any bundled ELF file still depends on one.
gpl_libs="libx264 libx265 libxvidcore"
for name in $gpl_libs; do rm -f "$appdir"/usr/lib/"$name".so*; done
command -v readelf >/dev/null 2>&1 || { echo "Missing tool: readelf" >&2; exit 1; }
for file in $(find "$appdir/usr" -type f \( -name '*.so' -o -name '*.so.*' \)); do
    for name in $gpl_libs; do
        if readelf -d "$file" 2>/dev/null | grep -q "NEEDED.*\[$name\."; then
            echo "$file depends on $name (GPL), which must not be bundled." >&2
            exit 1
        fi
    done
done
"$linuxdeploy" --appdir "$appdir" --output appimage
# Test the packaged entry point without requiring FUSE or a running display server.
run_app="$LDAI_OUTPUT --appimage-extract-and-run"
if ! "$LDAI_OUTPUT" --appimage-version >/dev/null 2>&1; then
    # Same Rosetta limitation as above: test the unpacked AppRun instead.
    offset="$(python3 -c '
import struct, sys
h = open(sys.argv[1], "rb").read(64)
shoff, = struct.unpack_from("<Q", h, 0x28)
shentsize, shnum = struct.unpack_from("<HH", h, 0x3A)
print(shoff + shentsize * shnum)' "$LDAI_OUTPUT")"
    unsquashfs -q -o "$offset" -d "$stage/unpacked" "$LDAI_OUTPUT" >/dev/null
    run_app="$stage/unpacked/AppRun"
fi
QT_QPA_PLATFORM=offscreen $run_app --help >/dev/null
if command -v xvfb-run >/dev/null 2>&1; then
    # --help checks the ELF loader; also initialize the real Widgets/QPA path.
    mkdir -p "$stage/gui-home"
    result=0
    HOME="$stage/gui-home" XDG_CONFIG_HOME="$stage/gui-home/.config" XDG_CACHE_HOME="$stage/gui-home/.cache" \
        xvfb-run -a timeout 4 $run_app >"$stage/gui-smoke.log" 2>&1 || result=$?
    if [ "$result" -ne 124 ]; then
        cat "$stage/gui-smoke.log" >&2
        echo "Packaged GUI exited unexpectedly (status $result)." >&2
        exit 1
    fi
fi
cp "$LDAI_OUTPUT" "$output"
chmod +x "$output"
echo "AppImage: $output"
