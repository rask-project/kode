"""A kode-remote's handshake over its stdio (serve): what CI checks of each
agent it builds, where Kode's tests cannot run (no Qt there).

Usage: python3 -I tools/remote-smoke.py AGENT VERSION
"""

import json
import struct
import subprocess
import sys


def main() -> int:
    agent, version = sys.argv[1], sys.argv[2]
    process = subprocess.Popen([agent, "serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def ask(message: dict) -> dict:
        payload = json.dumps(message).encode()
        process.stdin.write(struct.pack(">IBI", len(payload) + 5, 0, 0) + payload)
        process.stdin.flush()
        length, kind, channel = struct.unpack(">IBI", process.stdout.read(9))
        if kind != 0 or channel != 0:
            raise SystemExit(f"unexpected frame {kind} on channel {channel}")
        return json.loads(process.stdout.read(length - 5))

    early = ask({"jsonrpc": "2.0", "id": 1, "method": "system/ping"})
    assert early["error"]["code"] == -32002, early
    hello = ask({"jsonrpc": "2.0", "id": 2, "method": "initialize", "params": {"protocol": 1, "client": "smoke"}})
    result = hello["result"]
    assert result["agent"] == version, result
    assert len(result["session"]) == 32, result
    assert result["platform"]["os"] in ("linux", "darwin"), result
    pong = ask({"jsonrpc": "2.0", "id": 3, "method": "system/ping"})
    assert pong["result"] == {}, pong
    process.stdin.close()
    if process.wait(timeout=5) != 0:
        raise SystemExit("the agent did not exit cleanly")
    print(f"{agent}: {result['platform']['os']} {result['platform']['arch']}, ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
