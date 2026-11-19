# Features and usage

What SameBoy Qt can do and how to use it: controls, menus, peripherals, recording, debugging and the interface. Back to the [README](../README.md).

## Features

- Open `.gb` / `.gbc` ROMs (and Cocoa-compatible `.isx` images, whose `<name>.ram` is read when no `.sav` exists) with the homepage’s Open ROM button, dropping one file onto the window, using File → Open ROM,
  or passing a command-line path. Opening another path opens a separate game window; dropping a ROM onto a window always (re)starts it in
  that window. Dropping a save state onto a running game loads it. Explicit Hot Swap and Reload retain their replacement behavior.
- Automatic DMG / CGB / SGB selection based on the cartridge header, following Cocoa (CGB features → CGB, SGB features → SGB,
  Nintendo-licensed monochrome games → CGB for boot ROM colorization, otherwise DMG).
- Embedded open-source SameBoy boot ROMs.
- Integer scaling, nearest-neighbor rendering and preserved 160×144 aspect ratio.
- Independent emulation thread, frame pacing and bounded frame/audio buffers.
- 96 kHz stereo audio through Qt Multimedia, with 48 kHz fallback for devices that
  do not support 96 kHz. A pull buffer feeds the audio thread independently of GUI painting.
- Pause/resume; keyboard input is released when the window loses focus.
- Battery RAM/RTC saved beside the ROM as `game.sav` for `game.gb` / `game.gbc`: like Cocoa, once writes have been quiet
  for 0.25 s (and at least every five seconds while they never stop), on ROM replacement, on normal exit and when the
  session ends (log off/shut down). Writes use `QSaveFile`; on Windows a save briefly held open by another program is retried.
- Preferences (File → Preferences, `Ctrl+,`) with persistent user settings.
- GB/Pocket, GBC revisions, SGB/SGB2, GBA and Game Boy Player hardware choices;
  hardware and boot ROM changes take effect on the next ROM open.
- Live RTC, rewind duration, color correction, light temperature, monochrome
  palette, border, volume, high-pass and interference settings.
- All 16 upstream SameBoy scaling filters, including LCD/CRT, Scale2x/4x, HQ2x and
  OmniScale, through QRhi (Direct3D 11 on Windows; Metal on macOS; OpenGL on Linux).
- Gamma-correct Simple and Accurate frame blending using actual core frame parity
  and consecutive core frames; SGB automatically uses Simple blending.
- Four built-in monochrome palettes plus the 14 upstream presets, a palette editor,
  automatic/manual colors, brightness/hue controls and Cocoa-compatible `.sbp` import/export.
- Aspect/integer scaling and an on-screen display (frame rate, plus Cocoa's event messages such as the ROM title/CRC32,
  State saved/loaded, screenshots, recording and fast-forward/rewind/slow motion) with a configurable font.
- Configurable Player 1 keyboard, rapid A/B, held turbo, rewind and slow motion.
- PNG screenshots (`Ctrl+S`) at 1×/2×/4×/8×/16× integer scale (default 1×), optionally with display filters.
- A full cheat editor with per-entry enable/description/address/value/condition,
  GameShark/Game Genie import, and memory search with 8-bit/16-bit little/big-endian
  comparisons and the upstream `old`/`new` expression language. A search result's current value can be edited in place;
  as in Cocoa it is evaluated as a debugger expression (`$` for hex, registers and symbols) and written to memory.

## Controls

| Game Boy | Keyboard |
| --- | --- |
| D-pad | Arrow keys |
| A / B | X / Z |
| Start / Select | Enter / Backspace |
| Pause / resume | Ctrl+P |
| Open ROM | Ctrl+O |
| Preferences | Ctrl+, |
| Screenshot | Ctrl+S |
| Rapid A / Rapid B | A / S |
| Turbo (hold) | Tab |
| Rewind (hold) | R |
| Slow motion (hold) | Shift |

Turbo keeps audio enabled; the bounded buffer drops excess samples at high speeds
to avoid accumulating playback latency. Rewind reverses stereo sample frames within
each backwards-stepped game frame. Slow motion underclocks the core and keeps audio
continuous at the selected sample rate, with pitch following the slower emulation clock.
Unlimited turbo uses the available CPU; Preferences can cap it to 100–1000%. Rewind defaults to two minutes and can
be disabled. Input assignments and other settings are saved to a per-user INI
file using Qt's platform configuration location. OK applies/saves; Cancel discards.
Boot ROM folders must contain the filenames listed in `resources/bootroms`.

## Menus and Cocoa shortcut mapping

The menu bar follows `SameBoy/Cocoa/MainMenu.xib`, with selected Develop tools added as Debug:
File, Emulation, Cheats, Connect, Window, Debug and Help. Edit and SameBoy app menus are
omitted; text fields retain their native editing shortcuts and context menus.
About/Sponsor live in Help, and Preferences/Exit live in File. Menu spacing is compact
while respecting system fonts and DPI scaling. Only the active game/music page constrains the main window minimum size; hidden GBS controls do not prevent a small game window, and narrow menu bars use Qt’s overflow menu. Cocoa alternate
items (such as Quick Reset and Save Screenshot As) are always visible in Qt.
Command maps to Ctrl and Option maps to Alt on Windows/Linux. Conflicts present
in the Cocoa menus are resolved explicitly:

| Action | Shortcut |
| --- | --- |
| Reset / Quick Reset / Reload ROM | Ctrl+R / Ctrl+Alt+R / Ctrl+Shift+R |
| Pause / Mute | Ctrl+P / Ctrl+M |
| Save state slots 1–10 | Ctrl+1…9, Ctrl+0 |
| Load state slots 1–10 | Ctrl+Shift+1…9, Ctrl+Shift+0 |
| Screenshot / Save As / Copy | Ctrl+S / Ctrl+Alt+S / Ctrl+Shift+S |
| Start/stop audio recording (WAV, AIFF, raw PCM or VGM) | Ctrl+Shift+A |
| Enable cheats | Ctrl+Shift+C |
| Hot swap / New cartridge instance | Ctrl+Alt+O / Ctrl+N |
| Save ROM modifications / Save As | Ctrl+Alt+Shift+S / Ctrl+Alt+Shift+E |
| Increase/decrease window size | Ctrl++ (or Ctrl+=) / Ctrl+- |
| Minimize / Full screen | Ctrl+Alt+M / Ctrl+Alt+F |
| Close cartridge / Quit | Ctrl+W / Ctrl+Q |

Minimize is moved from Cocoa's Command+M to avoid Mute Sound. ROM export shortcuts
are moved to avoid Cocoa's duplicate screenshot shortcuts. Workboy remains disabled.

File → New Window opens another emulator instance (Ctrl+Shift+N). Open a ROM in each window, then Connect → Game Link Cable & Infrared → the other window. Connect → None disconnects both ends; replacing/closing either ROM also disconnects. Core execution is serialized and interleaved according to each model’s clock, as in Cocoa. Serial bits and infrared edges are forwarded directly, and pause/resume is shared. Linked sessions share Turbo pacing: either side can accelerate both consoles, using the triggering side’s cap (the faster request wins if both request Turbo; unlimited takes priority). Releasing all Turbo requests restores 1×. Analog trigger acceleration is shared too; rewind and slow-motion remain suppressed while linked. Starting the EXE again forwards a new-window/ROM request over a user-scoped local IPC endpoint to the existing process, so all windows can discover each other. A launch lock handles simultaneous startups and request IDs suppress duplicate retries. This is local in-process linking, not network multiplayer. GBS sessions cannot be linked.

Connect → Game Boy Printer attaches the core's printer protocol without opening a window; Connect → None disconnects it. The preview opens automatically only when a nonempty paper strip arrives.
The Printer window collects palette-mapped print strips with the same white margins as Cocoa, shows job completion,
and offers native-width PNG export, clearing the paper roll, and platform printing with pagination for long rolls.
Window → Show Printer reopens the preview. A new job after closing the window starts a fresh paper roll.
Qt PrintSupport is included in Windows deployment; the physical printer/PDF destination is chosen through the system print dialog.

States replace the ROM extension: `game.gb` / `game.gbc` → `game.s1`…`game.s10`. Older `.gb.s1` / `.gbc.s1` names remain readable when the new name is absent, and new names take precedence. Loading a state requires the
matching hardware revision. Cheats use SameBoy’s native binary format: `game.cht` beside `game.gb` / `game.gbc`, or `cheats.cht` inside a cartridge instance. No JSON persistence or migration is supported. GameShark/Game Genie code import in the editor remains available.
Cartridge instances are `.gbcart` folders containing a Cocoa-style `rom.gbl` link
and independent battery/state/cheat files; drag the folder onto the window to open.
Audio recording uses the negotiated playback sample rate (96 or 48 kHz) as 16-bit stereo WAV, AIFF or raw PCM;
VGM records the core's sound register log, as in Cocoa.
Slow-motion audio is included and follows the emulated frame timeline. At turbo speeds,
excess PCM can be dropped by the bounded frontend buffer.
Help → Audio and Performance shows the active sample rate, measured emulation speed,
underflow count and discarded audio frames. Startup/pause underflows alone do not
indicate insufficient CPU performance.

Windows executables embed an explicit `asInvoker` manifest. The application
does not request administrator privileges. Development sandbox permissions are
independent of the permissions required by the installed application.

Joy-Con arrangement is tested with SDL virtual left/right controllers, including the configuration UI,
L + R gesture, explicit separation while buttons remain held, reconnect identity and sensor selection.
Printer tests send real core protocol packets and verify margins/greyscale output, completion,
PNG round-trip, multi-page PDF output and reconnect. Physical Bluetooth/rumble behavior and real
printer drivers still need device-specific validation. Linux builds and tests run in Apple Container (all 31 tests pass on arm64 and x64).

Battery saves replace the ROM extension. If the new `.sav` does not exist, the previous `.gb.sav` / `.gbc.sav` is read as a fallback; subsequent saves use the new name and preserve the old file. If both exist, the new name takes precedence. Cartridge instances retain `cartridge.sav`.

## GBS music player

Open or drop a `.gbs` file in the same way as a ROM. The integrated player shows the title, composer,
copyright, track selector, restart, play/pause, previous/next controls and a stereo waveform.
Playback uses SameBoy's GBS loader and track switcher through the same audio pipeline; WAV recording works as usual.
The automated fixture verifies generated PCM and track initialization. Game-specific GBS and link compatibility
still need testing with representative commercial titles; synthetic protocol tests are not a compatibility guarantee.

## Game Boy Camera

Preferences → Camera (also Window → Camera Settings) selects a live camera, an image file, or core-generated noise.
A live device starts only when a Camera cartridge requests a picture; pausing or closing the ROM stops capture.
A request waits for a fresh video frame and freezes its 128×112 grayscale image until the next shot.
Image sources use auto-orientation, center cropping and optional horizontal mirroring. Exposure, edge enhancement
and dithering remain in the SameBoy core. Missing devices, denied permissions, backend errors and timeouts
complete the capture with a blank image rather than leaving the cartridge busy forever.
macOS includes NSCameraUsageDescription; physical device and permission behavior still require native testing.
The automated tests inject frames and use a real Camera cartridge's registers to verify completion, pixel output,
freshness and stable snapshots, and exercise Unicode-path image import without activating a physical camera.

The `.cht` serializer is tested byte-for-byte against upstream `GB_save_cheats`, including its magic header,
record size, bank/address fields, condition and enable flags. Loading validates the whole file before replacing
active entries, and saving an empty list writes a valid empty database to preserve deletions.

## Debug tools

Debug appears immediately before Help. It provides an editable hex/ASCII memory viewer with bank selection
and hexadecimal address navigation, using Cocoa’s five spaces (Entire Space, ROM, Video RAM, Cartridge RAM, RAM), and a VRAM viewer with Tileset, Tilemap, Objects and Palettes tabs.
ROM shows the fixed/mapped lower 16 KiB plus the selected upper 16 KiB bank; RAM shows fixed C000 plus the selected D000 bank, as in Cocoa. Double-click a hex byte to edit it. Mapped memory and I/O use core writes; raw ROM/VRAM/RAM views modify the selected bank. Cartridge RAM edits mark the battery dirty. ROM changes affect the running copy until Save ROM Modifications is used. Memory snapshots use side-effect-free reads or copied direct-access bank data; graphical previews use the core's
own debug renderers. Memory reads only its visible byte range and refreshes at 4 Hz, matching Cocoa and VRAM at up to 60 Hz, including while paused. Snapshots run asynchronously on the emulation thread with at most one request in flight per viewer; unchanged images and offscreen memory rows do not trigger repaints. Objects use four fixed columns of cards with a scrollable view; Tilemap's Scrolling overlay shows the 160×144 viewport from SCX/SCY, including wraparound. Opening another ROM or hot swapping retains tool windows and their geometry, refreshes their contents and resets cheat search results and save paths. Newly created game and tool windows prefer free space around the owning game window (including adjacent displays), and otherwise choose the position with the least overlap; existing window positions are retained.
Background and Window are toggled together, matching Cocoa; Objects has a separate switch. The channel toggles use Alt+1…4 (Option on macOS), as in Cocoa.
Four APU channels (two pulse, wave and noise) can be muted independently. These window-local choices are
reapplied after reset, state load and GBS track changes. Developer Mode enables the native SameBoy text debugger. Show Console provides command history, registers/backtrace, CPU usage and step controls; Ctrl+C breaks execution and Ctrl+K clears the console. Graphical Debugger is intentionally omitted. The original command parser remains in the core.

## Interface language and window chrome

First launch starts in English and shows a small English / 简体中文 chooser. Choosing a language saves it; the new language takes effect the next time the app is launched on every platform, and the chooser and Preferences both say so. The app has no restart button. The chooser explains that the language can be changed later in Preferences → General. Language settings offer only English and 简体中文. Existing explicit choices are preserved; older Follow system settings return to English and show the chooser once. `translations/update.py <lrelease>` checks source-string coverage and placeholders, regenerates the TS catalog and builds the embedded QM file.

The menu bar and Window → Show Status Bar default to enabled and are remembered. The game view’s right-click menu starts with Open ROM (and Show Menu Bar on Windows/Linux), and groups Cheats, Connect, Debug, pause/reset, state slots, Video (filters, blending, colors, border, aspect/integer scaling), Audio (mute and volume presets), Window (fullscreen and bars), screenshot and Preferences. Shared actions retain their shortcuts and enabled/checked state. On Windows/Linux, Show Menu Bar also stays at the top for quick recovery; macOS omits this action from the context menu; changing either bar preserves the game viewport size in a normal window. The status bar keeps the current ROM path (elided to fit, full path on hover) alongside FPS; temporary messages clear back to it. Alt does not activate or reveal the menu bar and can be mapped as a game key. Game focus is restored after menus and when returning from inspectors. On macOS, Command-W closes the game window; File → Close ROM unloads the cartridge while keeping the window open. Settings uses the native application menu and Command-comma shortcut; on Windows/Linux Preferences stays in File. Camera Settings remains in File (directly below Preferences on Windows/Linux); Help includes Sponsor Original SameBoy, and Workboy is omitted from Connect.

Audio preferences offer 44.1, 48 and 96 kHz (default 96 kHz). Unsupported device formats fall back to a supported rate; the core and output always use the same actual rate. Changing rate rebuilds audio output without resetting the game and finalizes any active WAV recording first.

VRAM palette values default to raw Game Boy colors (`$XXXX`, preserving the stored word including the unused high bit), with an optional RGB hex display. Color swatches use the core's color correction; monochrome hardware shows its 2-bit shade index instead of inventing a hardware RGB value.


AI debugging is opt-in through `sameboy-qt --debug-server [ROM]`. There is no AI menu toggle or persisted AI enable setting.
The same executable offers `sameboy-qt --rpc METHOD [JSON_PARAMS]`, using user-local JSON-RPC IPC. See [protocol and examples](ai-debugging.txt).
About shows the pinned full core commit and GitHub links. VRAM tilemap hover information
includes tile number, bank/VRAM address, map address and attributes; palette text contrast
matches Cocoa, and tileset grid boundaries distinguish banks and 128-tile sections.

[Third-party licensing review and remaining release requirements](third-party-review.txt)

Slow motion follows Cocoa by underclocking the core while retaining full-duration PCM output; it no longer drops audio. Debug → Audio Channels reuses the global Mute Sound action, including the game context menu. Printer controls and the preview viewport follow dialog palette changes without recoloring the printed feed.

Preferences → General → Theme offers Follow system (default), Light and Dark. Changes are previewed immediately; OK saves the choice and Cancel restores the previous appearance. Empty game windows use the application palette.

The RPC interface also exposes link cable/infrared connections, printer attachment/status and native PNG feed capture/clear; see the protocol reference. The game context menu shares the Emulated Model submenu with the main menu.

## Video recording and slow controls

Emulation → Start Video Recording (`Ctrl+Shift+V`) records MP4/H.264 with stereo AAC audio.
The game context menu shares the same action. Choose 1×/2×/4×/6×/8× output (default 4×),
with current filters and frame blending enabled by default. Without filters, scaling uses nearest neighbor.
Frames come from the core, not the GUI's latest-frame snapshot. Recording follows the original emulated
frame rate, including turbo/slow playback; audio is aligned to that timeline. The output size and sample
format stay fixed for one recording; incompatible changes stop recording explicitly.
A bounded queue reports encoder pressure instead of silently dropping frames. Failed/empty recordings
preserve the existing destination through QSaveFile. Available codecs depend on the Qt multimedia backend.
Custom video/audio frame input requires Qt's FFmpeg multimedia plugin; codec support
alone in a native backend such as AVFoundation is insufficient. Install a Qt build
with that plugin and use `QT_MEDIA_BACKEND=ffmpeg` if selecting the backend explicitly.
The video smoke test is skipped when the plugin or codecs are unavailable.
For Homebrew Qt 6.12.0 on macOS, run `./setup-macos-ffmpeg.sh` once. It builds a checksum-pinned
FFmpeg 9.0.2 from source as LGPL-only shared libraries (`--disable-gpl --disable-nonfree`, no x264/x265;
H.264/HEVC/AAC encoding uses Apple VideoToolbox/AudioToolbox), then a local Qt Multimedia against it.
Homebrew's FFmpeg is GPL and is deliberately not used. `./build-macos-arm64.sh` automatically uses the result
for compilation and tests, and refuses to package any bundled `libavutil` that is not LGPL.
The installations live in `.tools/ffmpeg-lgpl/install` and `.tools/qt-multimedia-ffmpeg/install`;
Homebrew libraries are left untouched. Shipping the app still requires including the LGPL notices and the
FFmpeg/Qt source offer (see `docs/third-party-review.txt`).
On this Windows ARM64 environment, H.264/AAC encode/decode and filtered 2× output are tested.

Controls include optional held 1/8 speed and 1/32 speed actions, initially unassigned.
Right-click a keyboard/controller binding to clear it. The new slow actions are keyboard bindings;
existing controller hotkey indices are retained. Audio buffering accommodates underclocked frames,
and debug requests are serviced during frame pacing waits.

The console CPU Load plot follows Cocoa's 256-frame history and shows the one-second emulated CPU
busy percentage. It updates while running; paused/debugger-stopped periods show zero activity.

For scripting, use `--rpc emulation.step` for an atomic input/frame/PNG operation and the standard-library
examples in `examples/`. This is a thin CLI example, not a separately maintained Python SDK.

On Windows, a native MoveSize timer bridge keeps game/recording refresh and posted paint delivery active while dragging or resizing any application window. Moving does not set the emulation pause flag; actual DWM/GPU drag behavior still needs manual validation on target systems.

Controller identity: serial numbers or device paths keep their existing saved identifiers.
If a backend provides neither, each connected controller gets a unique connection identifier
and a numbered label. Explicit assignments for such controllers must be selected again after
reconnection or restarting the application; physical identity cannot be inferred safely.

## Screenshot scale, ROM routing and audio devices

General settings provide 1×, 2×, 4×, 8× and 16× screenshot scales; 1× is the default.
The scale applies to PNG exports with or without filters. Unfiltered exports use
nearest-neighbor integer scaling, independently of the current window size.

Finder/OS opens, File → Open and recent files route by normalized absolute
path plus a SHA-256 snapshot of the loaded ROM. Reopening an unchanged path only
focuses its existing window; changed contents reload that window. Distinct paths
remain independent even when contents match. The initial empty window receives
the first file; subsequent files use separate windows. If two different paths
would share the same basename-based saves, the second path gets a remembered
full-filename namespace for battery, state and cheat files, preserving the first
window's files. Explicit Reload and Hot Swap remain available. A dropped ROM is the exception: it always
(re)starts in the window it was dropped on, except that a ROM whose saves are in use by another window is refused there.

Debug menu choices are session-only and reset on every application launch: Developer
Mode off, automatic CPU frequency, all audio channels and rendering layers enabled,
and sound unmuted. Older persisted debug choices are ignored.

Debug → Force CPU Frequency offers Automatic, 4 MHz (4,194,304 Hz) and 8 MHz
(8,388,608 Hz). Forced modes keep the CPU in single-speed or double-speed
throughout execution, including when the ROM requests a speed switch. The PPU
and audio retain their normal clock; game logic that depends on CPU instruction
throughput can still behave differently. Automatic returns speed control to the
ROM from the current mode. Debug Console shows the actual CPU clock in MHz.

Audio settings provide Follow system (default) and explicit output-device IDs.
An unavailable selected device temporarily falls back to the system output and
is selected again when it reconnects. Device notifications are queued/coalesced;
a healthy output is retained for unrelated or input-only changes. Rebuilding first
detaches the core's shared buffer, then stops the old sink on its audio thread,
and finally attaches the new stream. Recording is finalized before a stream
format/device change. Stale callbacks from old streams are ignored. Physical
hot-plug behavior still needs validation with the intended audio hardware.
