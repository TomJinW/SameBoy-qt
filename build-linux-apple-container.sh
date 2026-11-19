#!/bin/sh
set -eu
cd "$(dirname "$0")"

usage() {
    echo "Usage: ./build-linux-apple-container.sh [arm64|x64|all] [--appimage] [--test] [CMake options...]"
    echo "Default: arm64, without tests. Outputs: dist/linux-arm64 and dist/linux-x64."
    echo "Optional environment: JOBS=4 CONTAINER_MEMORY=8G"
}
case "${1:-arm64}" in
    -h|--help) usage; exit 0 ;;
    arm64|aarch64) targets=arm64 ;;
    x64|amd64|x86_64) targets=x64 ;;
    all) targets="arm64 x64" ;;
    *) usage >&2; exit 2 ;;
esac
if [ "$#" -gt 0 ]; then shift; fi
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo "This helper requires an Apple Silicon Mac. On Linux, use ./build-linux.sh." >&2
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
case "$jobs" in ''|*[!0-9]*|0) echo "JOBS must be a positive integer." >&2; exit 2 ;; esac
memory="${CONTAINER_MEMORY:-8G}"
# Images, guest writable layers, builds and AppImage extraction all use host disk.
free_kb=$(df -Pk "$PWD" | awk 'END {print $4}')
minimum_kb=8388608
if [ "$targets" = "arm64 x64" ]; then minimum_kb=12582912; fi
if [ "$free_kb" -lt "$minimum_kb" ]; then
    echo "Not enough free host disk space: $((free_kb / 1024)) MiB available." >&2
    echo "Free at least $((minimum_kb / 1024)) MiB before building $targets (15–20 GB recommended for all)." >&2
    exit 1
fi
container system start --enable-kernel-install
for target in $targets; do
    arch=arm64
    if [ "$target" = x64 ]; then arch=amd64; fi
    image="sameboy-qt-linux-builder:debian13-$arch"
    container build --arch "$arch" --cpus "$jobs" --memory "$memory" \
        --tag "$image" --file cmake/containers/linux.Containerfile cmake/containers
    # Linux builds/tests run inside the guest; each architecture has its own cache.
    # Only the small dependency recipe is sent to the image builder, not the source tree.
    if [ "$arch" = amd64 ]; then
        container run --rm --arch "$arch" --rosetta --cpus "$jobs" --memory "$memory" \
            --uid "$(id -u)" --gid "$(id -g)" \
            --volume "$PWD:/workspace" --workdir /workspace --env "JOBS=$jobs" \
            --env HOME=/tmp/sameboy-build-home --env LC_ALL=C.UTF-8 \
            "$image" sh ./build-linux.sh "$@"
    else
        container run --rm --arch "$arch" --cpus "$jobs" --memory "$memory" \
            --uid "$(id -u)" --gid "$(id -g)" \
            --volume "$PWD:/workspace" --workdir /workspace --env "JOBS=$jobs" \
            --env HOME=/tmp/sameboy-build-home --env LC_ALL=C.UTF-8 \
            "$image" sh ./build-linux.sh "$@"
    fi
    echo "Linux $target output: $PWD/dist/linux-$target/bin/sameboy-qt"
done
