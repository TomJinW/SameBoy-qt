"""Small stdlib-only example client for SameBoy Qt's generic RPC interface.

Start the application with --debug-server first. This intentionally uses the
single EXE as transport; it is an example helper, not a second emulator or SDK.
"""
import base64
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path


class RpcError(RuntimeError):
    def __init__(self, error):
        self.code = error.get("code")
        super().__init__(f"RPC {self.code}: {error.get('message', 'Unknown error')}")


class SameBoy:
    def __init__(self, executable, endpoint=None, timeout=20):
        self.executable = str(Path(executable).resolve())
        self.endpoint = endpoint
        self.timeout = timeout
        self.sequence = 0

    def call(self, method, **params):
        self.sequence += 1
        request = {"jsonrpc": "2.0", "id": self.sequence,
                   "method": method, "params": params}
        command = [self.executable, "--rpc"]
        if self.endpoint:
            command += ["--endpoint", self.endpoint]
        command += ["--stdin"]
        process = subprocess.run(
            command, input=json.dumps(request).encode("utf-8"),
            capture_output=True, timeout=self.timeout,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        try:
            response = json.loads(process.stdout)
        except (ValueError, UnicodeDecodeError) as error:
            raise RuntimeError(process.stderr.decode("utf-8", errors="replace")
                               or "No valid RPC response") from error
        if response.get("id") != request["id"]:
            raise RuntimeError("RPC response id mismatch")
        if "error" in response:
            raise RpcError(response["error"])
        if process.returncode or "result" not in response:
            raise RuntimeError("RPC transport failed")
        return response["result"]

    def windows(self):
        return self.call("windows.list")["windows"]

    def window(self, window_id=None):
        candidates = [item for item in self.windows()
                      if item["rom"] and (window_id is None or item["id"] == window_id)]
        if len(candidates) != 1:
            raise ValueError("Select one loaded ROM window by its id")
        return Game(self, candidates[0])


@dataclass(frozen=True)
class Step:
    frames: int
    width: int
    height: int
    png: bytes

    def save(self, path):
        Path(path).write_bytes(self.png)


class Game:
    def __init__(self, client, metadata):
        self.client = client
        self.id = metadata["id"]
        self.generation = metadata["generation"]

    def call(self, method, **params):
        # Keep the original generation: replacing the ROM invalidates this object.
        return self.client.call(method, **params, window=self.id,
                                generation=self.generation)

    def step(self, buttons=(), frames=1, player=1):
        result = self.call("emulation.step", buttons=list(buttons),
                           frames=frames, player=player)
        return Step(result["frames"], result["width"], result["height"],
                    base64.b64decode(result["base64"], validate=True))

    def read_memory(self, address, size=1, space="entire", bank=0):
        return bytes.fromhex(self.call("memory.read", address=address,
                                      count=size, space=space, bank=bank)["hex"])

    def screenshot(self):
        return base64.b64decode(self.call("screen.capture")["base64"], validate=True)

    def pause(self):
        return self.call("emulation.pause")
