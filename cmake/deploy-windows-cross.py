#!/usr/bin/env python3
"""Deploy Qt for a Windows executable that was cross-compiled on Linux (windeployqt cannot run there).

Copies the Qt plugins the application needs, then every DLL they and the executable import,
following imports with the mingw-w64 objdump. Mirrors what windeployqt puts in bin/ and bin/plugins.
"""
import os
import shutil
import subprocess
import sys

PLUGINS = [
    "platforms/qwindows.dll", "styles/qmodernwindowsstyle.dll", "imageformats/qgif.dll", "imageformats/qico.dll",
    "imageformats/qjpeg.dll", "imageformats/qsvg.dll", "iconengines/qsvgicon.dll", "generic/qtuiotouchplugin.dll",
    "networkinformation/qnetworklistmanager.dll", "tls/qcertonlybackend.dll", "tls/qschannelbackend.dll",
    "multimedia/ffmpegmediaplugin.dll", "multimedia/windowsmediaplugin.dll",
]
# Loaded at run time rather than linked, so the import scan cannot see them.
EXTRA_DLLS = ["opengl32sw.dll", "d3dcompiler_47.dll"]


def imports(path, objdump):
    out = subprocess.run([objdump, "-p", path], check=True, capture_output=True, text=True).stdout
    return [line.split(":", 1)[1].strip() for line in out.splitlines() if line.strip().startswith("DLL Name:")]


def main():
    exe, qt_root, runtime_dirs, dest = sys.argv[1], sys.argv[2], sys.argv[3].split(os.pathsep), sys.argv[4]
    objdump = os.environ.get("OBJDUMP", "x86_64-w64-mingw32-objdump")
    search = [os.path.join(qt_root, "bin")] + runtime_dirs
    bin_dir = os.path.join(dest, "bin")
    plugin_dir = os.path.join(bin_dir, "plugins")
    files = [os.path.join(bin_dir, os.path.basename(exe))]
    for plugin in PLUGINS:
        source = os.path.join(qt_root, "plugins", plugin)
        if not os.path.isfile(source):
            sys.exit("Qt plugin missing from the kit: " + plugin)
        target = os.path.join(plugin_dir, plugin)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(source, target)
        files.append(target)
    deployed = set()
    pending = list(files)
    for name in EXTRA_DLLS:
        pending.append(name)
    while pending:
        item = pending.pop()
        if os.sep not in item and not os.path.isabs(item):
            name = item
            if name.lower() in deployed:
                continue
            location = next((os.path.join(d, name) for d in search if os.path.isfile(os.path.join(d, name))), None)
            if location is None:
                continue  # a Windows system DLL
            deployed.add(name.lower())
            shutil.copy2(location, os.path.join(bin_dir, name))
            item = location
        pending.extend(imports(item, objdump))
    print("Deployed %d DLLs and %d plugins." % (len(deployed), len(PLUGINS)))
    # Never ship anything but an LGPL FFmpeg.
    for name in sorted(os.listdir(bin_dir)):
        if name.lower().startswith("avutil"):
            data = open(os.path.join(bin_dir, name), "rb").read()
            if b"libavutil license: LGPL" not in data or b"nonfree and unredistributable" in data:
                sys.exit("Bundled FFmpeg is not LGPL: " + name)
    with open(os.path.join(bin_dir, "qt.conf"), "w") as conf:
        conf.write("[Paths]\nPrefix = .\nPlugins = plugins\n")


main()
