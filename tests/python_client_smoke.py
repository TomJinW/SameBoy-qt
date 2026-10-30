"""Integration check against the temporary server owned by menu-smoke."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples"))
from sameboy import SameBoy, RpcError

client = SameBoy(sys.argv[1], endpoint=sys.argv[2])
game = client.window()
result = game.step(frames=2)
assert result.frames == 2 and result.width == 160 and result.height == 144
assert result.png.startswith(b"\x89PNG\r\n\x1a\n")
assert game.screenshot() == result.png
assert len(game.read_memory(0xC100, size=4)) == 4
try:
    game.step(buttons=["invalid"])
except RpcError as error:
    assert error.code == -32602
else:
    raise AssertionError("Invalid button was accepted")
print("Python thin client: step, stable PNG, memory and RPC errors passed")
