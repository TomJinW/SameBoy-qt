# Development notes

Project structure and current scope, validation status and the application icon tooling. Back to the [README](../README.md).

## Structure and current scope

`cmake/core` builds the upstream C core independently. `Emulator` owns the core
on a worker thread and exchanges image/PCM snapshots with the UI. `MainWindow`
handles files, audio output, menus and input; `ScreenWidget` paints the latest
frame. File access uses Qt buffers to support Unicode paths on Windows and Linux.

Controllers use a pinned static SDL 3 input backend (Windows ARM64 and Linux); Qt still owns windows, rendering and audio.
Preferences → Controls offers four independent keyboard/controller profiles, explicit device assignment,
a 15-button configuration wizard, deadzone, mouse/stick/sensor motion, analog triggers,
faux analog directions, two hotkeys (Cocoa's pause, reset, mute and save/load slots 1–10, plus screenshot),
rumble mode/strength and background-controller input.
When background input is enabled, only the most recently active visible game receives it;
it is not broadcast to every ROM window. Modal dialogs block controller input.
Rumble strength uses Cocoa’s response curve (`amplitude ** strength * strength`),
with the percentage divided by 100; 100% preserves the core amplitude and 0% is silent.
Automatic assignment gives each player a different connected gamepad; explicit disconnected assignments never silently steal another device.
Mouse motion follows Cocoa: its mode starts enabled, controller button activity switches to the controller, and a mouse click re-enables it. Left-click presses A; dragging does not change tilt. Leaving the game retains tilt. Coordinates use the actual image rectangle with Cocoa border compensation; the sensitivity setting remains available.
Four-player input applies to compatible Super Game Boy games, not GB/GBC link-cable multiplayer.
Preferences → Controls → Arrange Joy-Cons supports single horizontal/vertical grips, remembered left/right pairs,
automatic combining, explicit Combine/Separate, and the Cocoa L + R arrangement gesture.
Pair the devices through system Bluetooth first. Two combined halves occupy one player slot; their rumble
is routed to both devices and only the right-hand motion sensor is used. Explicitly separated devices stay single
until combined again or automatic combining is re-enabled. If a half disconnects, its buttons release immediately
and the pair retains its player identity for reconnection.
Physical controller compatibility depends on SDL/device drivers. The interactive debugger and console are not exposed.
The status bar shows emulated frames per second and the frames actually drawn, e.g. `59.7 FPS · 59.7 drawn`.

The SDL source archive is checksum-pinned in `cmake/controllers.cmake`; CMake fetches it on the first build,
or uses `.tools/SDL/SDL3-3.4.18.tar.gz` for offline builds. Its license is packaged alongside SameBoy's.
The core debugger is compiled for its cheat-search expression parser, but interactive
debugger execution remains disabled. GPU shaders are compiled by `qsb` from the
unmodified upstream GLSL filter bodies. QRhi currently uses GuiPrivate headers, so
build and deploy against the same Qt version; the Windows package bundles matching libraries.
Windows, macOS and Linux validation results are recorded separately: macOS and Linux are built and tested
(Linux in Apple Container); Windows x64 is cross-compiled and was tested on Windows by the author; real devices are unvalidated everywhere.

The SameBoy Qt frontend is released under the MIT License (`LICENSE`). The upstream core and boot ROM license is in `SameBoy/LICENSE`; Qt, FFmpeg and SDL keep their own licenses (`resources/licenses/THIRD-PARTY-NOTICES.txt`).

## Validation

The Windows x64 toolchain uses Qt 6.10.0's MinGW kit (MinGW GCC 13); the Windows ARM64 development toolchain uses Qt 6.10.0, native ClangCL 22 and the MSVC ABI.
The previous x64 build used MinGW 13.1. Core smoke tests boot
original synthetic cartridges across all selectable model families, verify cartridge
code execution, audio callbacks, rewind and battery serialization/restoration. Worker tests exercise
real-time frame pacing, nonuniform rendered content, audio production, pause,
shutdown, live border/filter changes and Unicode-path battery persistence.
Settings tests exercise INI persistence, hardware/boot mappings, all four page
layouts and filtered screenshot output. GPU tests execute all 16 filters and verify
Simple/Accurate blending, palette serialization and the 14 defaults. Audio tests cover
96 kHz generation, pull buffering, turbo/rewind audio and stereo alignment. Cheat tests
cover editing, persistence, custom expressions and endian search. The Linux tests run in the Apple Container guest under Xvfb.

An original interactive demo can be generated without downloading a game:

```sh
python tests/generate_demo.py build/demo.gb
```

Open it in the frontend to see scrolling stripes; the A button changes the palette.

## Application icon

The icon is original artwork drawn by `tools/make-icon.py` (needs Pillow; run it again to regenerate every size).
It writes `resources/icons/sameboy-qt.png` (embedded as the window icon on Windows and Linux),
`sameboy-qt.ico` (compiled into `sameboy-qt.exe`), `sameboy-qt.icns` (the macOS bundle icon, made with `iconutil`)
and `resources/icons/linux/<size>.png` (installed into the hicolor theme and used by the AppImage).
