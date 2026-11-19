"""Run after starting the app with --debug-server game.gbc.

Usage: python play_example.py PATH/TO/EXECUTABLE [WINDOW_ID]
EXECUTABLE is bin\\sameboy-qt.exe (Windows), sameboy-qt.app/Contents/MacOS/sameboy-qt (macOS),
bin/sameboy-qt or the AppImage file (Linux).
The example holds Right for eight game frames, then pauses and saves a PNG.
"""
import sys
from sameboy import SameBoy

client = SameBoy(sys.argv[1])
game = client.window(sys.argv[2] if len(sys.argv) > 2 else None)
result = game.step(buttons=["right"], frames=8)
result.save("step.png")
print(f"Completed {result.frames} frames; saved {result.width}x{result.height} PNG")
