#!/bin/sh
# Cross-compile the Windows x64 package with mingw-w64 on Linux; ./build-windows-x64-apple-container.sh runs it in a
# container on a Mac. Needs Qt 6.10.0's MinGW kit (target libraries) and the same version's Linux host kit (moc, rcc, qsb).
# Homebrew's mingw-w64 on macOS does not work: it links the UCRT, while Qt's MinGW kit uses the older msvcrt.
set -eu
cd "$(dirname "$0")"
if [ "$#" -gt 0 ]; then echo "Usage: ./build-windows-x64-mingw.sh" >&2; exit 2; fi
qt_root="${QT_X64_ROOT:-$PWD/.tools/Qt/6.10.0/mingw_64}"
if [ "$(uname -m)" = aarch64 ] || [ "$(uname -m)" = arm64 ]; then host_default="$PWD/.tools/Qt/6.10.0/gcc_arm64"
else host_default="$PWD/.tools/Qt/6.10.0/gcc_64"; fi
qt_host="${QT_HOST_ROOT:-$host_default}"
for path in "$qt_root/bin/Qt6Core.dll" "$qt_host/bin/qsb"; do
    if [ ! -e "$path" ]; then echo "Missing Qt kit file: $path (run ./build-windows-x64-apple-container.sh to install the kits)." >&2; exit 1; fi
done
build_dir=build-windows-x64-mingw
# A build tree (including FetchContent's nested ones) remembers the path it was configured from; the same folder is
# /workspace in the container and a different path elsewhere, so start over when that changes.
if [ "$(cat "$build_dir/.configured-from" 2>/dev/null || true)" != "$PWD" ]; then rm -rf "$build_dir"; fi
mkdir -p "$build_dir"; echo "$PWD" > "$build_dir/.configured-from"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
    -DCMAKE_PREFIX_PATH="$qt_root" -DQT_HOST_PATH="$qt_host" -DBUILD_TESTING=OFF
cmake --build "$build_dir" --parallel "${JOBS:-4}"
dist_dir="$PWD/dist/windows-x64"
rm -rf "$dist_dir"
cmake --install "$build_dir" --prefix "$dist_dir"
# The compiler knows where its own runtime DLLs are (they must match the compiler that built the executable).
cxx="$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++)"
runtime_dirs=""
for name in libstdc++-6.dll libwinpthread.dll.a; do
    directory="$(cd "$(dirname "$("$cxx" -print-file-name="$name")")" && pwd)"
    runtime_dirs="$runtime_dirs:$directory:$directory/../bin"
done
python3 -I cmake/deploy-windows-cross.py "$dist_dir/bin/sameboy-qt.exe" "$qt_root" "${runtime_dirs#:}" "$dist_dir"
echo "Built: $dist_dir/bin/sameboy-qt.exe"
