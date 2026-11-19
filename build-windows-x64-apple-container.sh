#!/bin/sh
# Cross-compile the Windows x64 package on an Apple Silicon Mac with Apple Container (mingw-w64 in Debian 13).
# The result cannot be run or tested on macOS. Output: dist/windows-x64/bin/sameboy-qt.exe
set -eu
cd "$(dirname "$0")"
if [ "${1:-}" = -h ] || [ "${1:-}" = --help ]; then
    echo "Usage: ./build-windows-x64-apple-container.sh"
    echo "Optional environment: JOBS=4 CONTAINER_MEMORY=8G"
    exit 0
fi
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo "This helper requires an Apple Silicon Mac. On Linux, use ./build-windows-x64-mingw.sh." >&2
    exit 1
fi
if ! command -v container >/dev/null 2>&1; then
    echo "Install Apple Container first: https://github.com/apple/container/releases" >&2
    exit 1
fi
if [ ! -f SameBoy/Core/gb.h ]; then
    echo "Initialize the project's SameBoy submodule before building." >&2
    exit 1
fi
jobs="${JOBS:-4}"
memory="${CONTAINER_MEMORY:-8G}"
qt_version=6.10.0
image=sameboy-qt-windows-builder:debian13
container system start --enable-kernel-install
container build --cpus "$jobs" --memory "$memory" --tag "$image" --file cmake/containers/windows-x64.Containerfile cmake/containers
run() {
    container run --rm --cpus "$jobs" --memory "$memory" --uid "$(id -u)" --gid "$(id -g)" \
        --volume "$PWD:/workspace" --workdir /workspace --env "JOBS=$jobs" --env HOME=/tmp/sameboy-build-home \
        "$image" "$@"
}
# Target libraries (Windows MinGW kit) and host tools (Linux kit) of the same Qt version; kept in .tools.
if [ ! -e ".tools/Qt/$qt_version/mingw_64/bin/Qt6Core.dll" ]; then
    run /opt/aqt/bin/aqt install-qt windows desktop "$qt_version" win64_mingw -m qtmultimedia qtshadertools --outputdir /workspace/.tools/Qt
fi
if [ ! -e ".tools/Qt/$qt_version/gcc_arm64/bin/qsb" ]; then
    run /opt/aqt/bin/aqt install-qt linux_arm64 desktop "$qt_version" linux_gcc_arm64 -m qtshadertools --outputdir /workspace/.tools/Qt
fi
run sh ./build-windows-x64-mingw.sh "$@"
