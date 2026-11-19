# Building SameBoy Qt

Requirements and build instructions for Windows, macOS and Linux, including cross-compiling and AppImage packaging. Back to the [README](../README.md).

## Scripts at a glance

| Target | Script | Run it on | Output |
| --- | --- | --- | --- |
| Windows x64 | `build-windows-x64.cmd [test\|build] ` | Windows (MinGW kit) | `dist/windows-x64/bin/sameboy-qt.exe` |
| Windows x64 (cross-compile) | `build-windows-x64-apple-container.sh` | Apple Silicon Mac | `dist/windows-x64/bin/sameboy-qt.exe` |
| Windows ARM64 (development) | `build-windows-arm64.cmd [test]` | Windows ARM64 | `dist/windows-arm64/bin/sameboy-qt.exe` |
| macOS arm64 | `build-macos-arm64.sh [--test]` | Apple Silicon Mac | `dist/macos-arm64/sameboy-qt.app` |
| Linux (native) | `build-linux.sh [--test] [--appimage]` | Linux | `dist/linux-<arch>/` |
| Linux arm64 / x64 | `build-linux-apple-container.sh [arm64\|x64\|all] [--test] [--appimage]` | Apple Silicon Mac | `dist/linux-<arch>/` and the AppImage |

Helpers: `setup-macos-ffmpeg.sh` (one-time LGPL FFmpeg and Qt Multimedia for video recording on macOS),
`package-linux-appimage.sh` (packages an existing native Linux build). Tests are not compiled unless you pass
`test` (Windows) or `--test`; the Windows cross-compile cannot run them.

## Requirements

- CMake 3.25+, Ninja, Qt 6.8+ Widgets, Multimedia and PrintSupport, matching GuiPrivate development
  headers, and the Qt Shader Tools `qsb` and Linguist `lrelease` host executables.
- Windows x64: **Qt 6.10 MinGW x64 kit** with its MinGW GCC (or cross-compile on a Mac, see below).
- Windows ARM64 (development only): **Qt MSVC ARM64 kit**, Visual Studio C++ ARM64 SDK/libraries,
  and ClangCL. Plain MSVC `cl.exe` cannot compile the core's GNU C extensions.
- Linux: GCC or Clang, Qt development packages, and a functioning Qt platform plugin
  for Wayland or X11. ARM64 should use a native ARM64 Qt/compiler toolchain.

## Generic CMake build

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/kit
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Windows x64 (release platform)

`build-windows-x64.cmd` uses Qt's MinGW x64 kit and MinGW GCC.
Set `QT_X64_ROOT`, `QT_HOST_ROOT` (Qt host tools, including `qsb.exe`) and
`MINGW_ROOT` to use installations outside `.tools`; the script builds, then packages
Qt and the MinGW runtime DLLs. Pass `test` to build and run the tests before packaging, or
`build` to only build (no tests, no packaging).
The x64 package is written to `dist/windows-x64/bin/sameboy-qt.exe`.

On an Apple Silicon Mac the Windows x64 package can be cross-compiled instead, without Windows:

```sh
./build-windows-x64-apple-container.sh
```

It builds a Debian 13 image with mingw-w64 (Apple Container), downloads Qt 6.10.0's MinGW kit and a Linux host kit
into `.tools/Qt` with aqtinstall, compiles with `cmake/toolchains/mingw-w64-x86_64.cmake`, and deploys Qt with
`cmake/deploy-windows-cross.py` (windeployqt cannot run on Linux; it refuses a non-LGPL FFmpeg). The result is
`dist/windows-x64/bin/sameboy-qt.exe`, with the same contents as the native MinGW package except Windows SDK's
`dxcompiler.dll`/`dxil.dll`. The tests cannot run on macOS, so the cross build only compiles and packages. On Linux, run `./build-windows-x64-mingw.sh` directly.

## Windows ARM64 (development only)

The Windows ARM64 helper is kept for development; no release package is planned for it. It is the only Windows
preset in `CMakePresets.json` (`windows-arm64`, for an already initialized developer shell). The helper initializes the
ARM64 Visual Studio environment, uses ClangCL, builds and deploys Qt. Test programs are
not compiled by default; pass `test` to build and run them before packaging:

```powershell
.\build-windows-arm64.cmd
```

By default the helper uses `.tools/Qt/6.10.0/msvc2022_arm64` for target libraries
and `.tools/Qt/6.10.0/msvc2022_64` for Qt host tools. Set `QT_ARM64_ROOT` and
`QT_HOST_ROOT` to use other installations.
The ARM64 package is written to `dist/windows-arm64/bin/sameboy-qt.exe`.

## Windows packages

Windows builds are portable by default: settings are saved to `bin/config/SameBoyQt/SameBoyQt.ini` beside the
executable. On the first launch an existing `%APPDATA%\SameBoyQt\SameBoyQt.ini` is copied there. If the folder is not
writable (for example under Program Files) the app falls back to `%APPDATA%`. Configure with `-DSAMEBOY_PORTABLE=OFF`
to always use `%APPDATA%`. Per-ROM isolated-storage flags are keyed by the ROM's absolute path, so they do not follow
a ROM that moves to another location or drive letter.

Each Windows package is self-contained in its `bin` folder: Qt plugins are in `bin/plugins`
(referenced by `bin/qt.conf` relative to the executable), and licenses, docs and examples are in
`bin/share`. The contents of `bin` can be copied to a folder of any name and still start.
Linux continues to use the generic GCC/Clang CMake commands above and system Qt libraries.

## macOS ARM64

On an Apple Silicon Mac, install Xcode command-line tools, CMake, Ninja and a Qt 6 macOS kit
including Shader Tools, Multimedia, PrintSupport and private headers. Set `QT_MACOS_ROOT` to the kit,
or use Homebrew Qt (`brew install cmake ninja qt`). Then run:

```sh
sh build-macos-arm64.sh
```

The preset targets arm64 and macOS 14+. The helper builds, deploys matching Qt frameworks/plugins,
ad-hoc signs, verifies the arm64 executable, and produces `dist/macos-arm64/sameboy-qt.app`.
Test programs are not compiled by default; run `sh build-macos-arm64.sh --test` to build and run them.
Metal is used for both live filters and filtered screenshots, and all shader bundles include MSL.
Finder file-open events and ROM/GBS/cartridge document types are registered; `.sav` is not registered.
The package is ad-hoc signed for local use, not a notarized Developer ID distribution. The build, deployment and all 31
automated tests have been run on a Mac; real Bluetooth controllers, sound hardware and printers have not been validated.

The upstream submodule was fetched and checked against `origin/master`; the current upstream HEAD is
`ea667ad79aa28409f034b1fd6da7304e1844750d` (SameBoy v1.0.5), matching the pinned revision.

## Native Linux build

Run the native helper on Linux, including inside a Linux VM or container:

```sh
./build-linux.sh
```

The helper detects the host architecture, configures a Release build with Ninja,
builds, and installs to `dist/linux-arm64` or `dist/linux-x64`. Test programs are not
compiled by default; pass `--test` to build and run them before installing (`./build-linux.sh --test`).
The executable is `bin/sameboy-qt` under that directory. This is a local installation
using system runtime libraries, not a standalone distributable bundle.

Install CMake 3.25+, Ninja, a C/C++ compiler, and Qt 6.8+ development packages
for Widgets, Multimedia, Network and PrintSupport, matching GuiPrivate headers,
Shader Tools (`qsb`) and Linguist tools (`lrelease`). Distribution package names and Qt versions vary; older
distributions may require a separate Qt installation. SDL3 is built from the pinned
source archive automatically; Linux controller support also needs the relevant
udev/HID development dependencies. The first build needs network access unless
the SDL archive is already available in `.tools/SDL`.

For a custom Qt kit and a limited number of build jobs:

```sh
QT_LINUX_ROOT=/path/to/Qt/6.x/gcc_64 JOBS=4 ./build-linux.sh
```

Additional arguments are forwarded to CMake, for example
`./build-linux.sh -DCMAKE_BUILD_TYPE=Debug`. Headless builds require Xvfb,
`xvfb-run`, xauth and a working software OpenGL implementation to run GUI/GPU
tests. Desktop builds use the current display. Actual audio, GPU and controller
compatibility still needs validation on the target Linux desktop.

## Linux builds on Apple Silicon with Apple Container

One helper supports both CPU architectures; separate scripts are unnecessary:

```sh
./build-linux-apple-container.sh arm64
./build-linux-apple-container.sh x64
./build-linux-apple-container.sh all
```

Install Apple Container on a supported Apple Silicon Mac first. Reserve 15–20 GB
of free disk space for both architectures; the helper checks for at least 12 GiB
with `all` (8 GiB for one architecture). The helper starts
its service, builds a cached Debian 13 dependency image for each selected architecture,
then runs `build-linux.sh` in the Linux guest. x64 uses the `amd64` image with Rosetta.
The small image recipe is in `cmake/containers/linux.Containerfile`; it provides Qt
6.8, private headers, Shader Tools, SDL build dependencies, Xvfb and software OpenGL.
The first build installs the default Linux kernel if needed, downloads the base image/packages, and may fetch the SDL archive.

Sources are bind-mounted at `/workspace`. Guest builds/tests run as the invoking
Mac user’s UID/GID, so file ownership and read-only save tests behave correctly. Build caches stay in `build-linux-arm64`
and `build-linux-x64`, and installed binaries are exported to `dist/linux-arm64/bin`
and `dist/linux-x64/bin` on the Mac. These installations depend on compatible Linux
system libraries and do not run on macOS. Use `--appimage` below for portable bundles.
GUI tests run under Xvfb; actual Linux desktop/audio/controller behavior still needs
hardware validation. ARM64 and x64 builds run sequentially when using `all`.

```sh
JOBS=4 CONTAINER_MEMORY=8G ./build-linux-apple-container.sh all -DCMAKE_BUILD_TYPE=Debug
```

Windows helpers use `build-windows-x64` and `build-windows-arm64` as their respective
build directories. Older `build-arm64`/`build-x64` caches are not reused; generate a
fresh build in the new directories rather than moving an existing CMake cache.

### AppImage packaging

```sh
# On an Apple Silicon Mac: build and package both Linux architectures (add --test to run tests).
./build-linux-apple-container.sh all --appimage
# Or just one architecture:
./build-linux-apple-container.sh x64 --appimage
# On native Linux:
./build-linux.sh --appimage
# Package an already-built native Linux tree without rebuilding:
./package-linux-appimage.sh
```

Outputs are `dist/linux-arm64/SameBoyQt-0.1.0-aarch64.AppImage` and
`dist/linux-x64/SameBoyQt-0.1.0-x86_64.AppImage`. Each file is for its own CPU
architecture. The helper downloads and caches the official linuxdeploy and Qt
plugin AppImages in `.tools/appimage-*`, bundles Qt/backends and dependencies,
and checks the packaged `--help` entry point without FUSE. When Xvfb is installed,
it also checks that the real GUI starts, using temporary settings. Native packaging also
requires curl, patchelf, file, squashfs-tools, desktop-file-utils, qmake6 and
Qt Declarative development tools (`qmlimportscanner`) and Qt SVG plugins;
the container image installs these automatically. `QMAKE` can select a custom Qt.

The builder uses Debian 13 (glibc 2.41); this is the tested runtime baseline.
Compatibility with older distributions is not established. AppImage does not make a new glibc compatible with
older distributions. Build on an older Linux base with a suitable Qt kit when
older-distribution support is needed. Actual desktop, sound and controller use
still needs validation on each target system.

The container image builds an LGPL-only FFmpeg 7.1.5 (no x264/x265; OpenH264, libvpx and libopus supply the codecs)
because Debian's own FFmpeg is a GPL build. `package-linux-appimage.sh` refuses to bundle a `libavutil` that is not
LGPL, removes any libx264/libx265/libxvidcore that linuxdeploy copied in, and fails if a bundled library still needs them.
