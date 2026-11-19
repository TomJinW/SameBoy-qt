#!/bin/sh
set -eu
cd "$(dirname "$0")"

if [ "$(uname -s)" != Linux ]; then
    echo "Run this helper inside Linux (native machine, VM or Linux container)." >&2
    exit 1
fi
case "$(uname -m)" in
    aarch64|arm64) arch=arm64 ;;
    x86_64|amd64) arch=x64 ;;
    *) arch="$(uname -m)" ;;
esac

mkdir -p "$HOME"

# Leave Qt discovery to CMake for distribution packages; allow a custom Qt kit.
if [ -n "${QT_LINUX_ROOT:-}" ]; then
    export CMAKE_PREFIX_PATH="$QT_LINUX_ROOT${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export PATH="$QT_LINUX_ROOT/bin:$PATH"
fi
for tool in cmake ctest ninja cc c++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool. Install CMake, Ninja and a C/C++ compiler." >&2
        exit 1
    fi
done
if [ ! -f SameBoy/Core/gb.h ]; then
    echo "SameBoy core is missing. Initialize the project's SameBoy submodule first." >&2
    exit 1
fi

appimage=0
run_tests=0
# Leading helper flags, in any order; tests run only with --test.
while [ "$#" -gt 0 ]; do
    case "$1" in
        --appimage) appimage=1; shift ;;
        --test) run_tests=1; shift ;;
        *) break ;;
    esac
done
build_dir="$PWD/build-linux-$arch"
dist_dir="$PWD/dist/linux-$arch"
build_testing=OFF
if [ "$run_tests" = 1 ]; then build_testing=ON; fi
# Extra arguments are passed to CMake, e.g. -DCMAKE_BUILD_TYPE=Debug.
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$build_testing" "$@"
cmake --build "$build_dir" --parallel "${JOBS:-$(getconf _NPROCESSORS_ONLN)}"

# A failing test is reported at the end; it must not block packaging.
tests_ok=1
# Widgets/OpenGL tests need a display. Xvfb allows running them on a build server.
if [ "$run_tests" != 1 ]; then
    :
elif [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "Headless tests require xvfb-run (install Xvfb and xauth), or a desktop session." >&2
        exit 1
    fi
    xvfb-run -a ctest --test-dir "$build_dir" --output-on-failure || tests_ok=0
else
    ctest --test-dir "$build_dir" --output-on-failure || tests_ok=0
fi
cmake --install "$build_dir" --prefix "$dist_dir"
echo "Built: $dist_dir/bin/sameboy-qt"
echo "This installation uses system Qt and other Linux runtime libraries."

if [ "$appimage" = 1 ]; then ./package-linux-appimage.sh; fi
if [ "$tests_ok" != 1 ]; then
    echo "WARNING: some tests FAILED, but the distribution was still produced. Review the test output above." >&2
fi
