"""Run after: sameboy-qt.exe --debug-server game.gbc

Usage: python play_example.py PATH/TO/sameboy-qt.exe [WINDOW_ID]
The example holds Right for eight game frames, then pauses and saves a PNG.
"""
import sys
from sameboy import SameBoy

client = SameBoy(sys.argv[1])
game = client.window(sys.argv[2] if len(sys.argv) > 2 else None)
result = game.step(buttons=["right"], frames=8)
result.save("step.png")
print(f"Completed {result.frames} frames; saved {result.width}x{result.height} PNG")
