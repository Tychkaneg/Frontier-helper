"""Create the v1.0.1 GUI from the exact public v1.0.0 executable.
Only UTF-16 strings change; executable sections must remain byte-identical.
The FPS-sync camera DLL is shipped separately and is never rebuilt here.
"""
from pathlib import Path
import hashlib
import struct
import sys

ORIGINAL_SHA256 = "fbcf5fa614738a818c7ac3c4ebffa873fdcc2784080a2e82d60a890ffdee8cdf"

def patch(data):
    if hashlib.sha256(data).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("Unsupported original Helper EXE; no patch written")
    replacements = [
        ("FrontierOrbitRain08.dll", "FrontierTargetL3Mod.dll", 3),
        ("1.0.0", "1.0.1", 3),
        ("F8 toggles camera  \u00b7  L3 recenters in game",
         "F8 toggles camera  \u00b7  L3 targets monsters", 1),
        ("[start] Camera enabled. Right stick / L3 reset / F8 toggle.",
         "[start] Camera enabled. Right stick / L3 aim / F8 toggle.", 1),
    ]
    updated = data
    for before, after, count in replacements:
        old = before.encode("utf-16le")
        new = after.encode("utf-16le")
        if len(new) > len(old) or updated.count(old) != count:
            raise ValueError("Unexpected string layout; no patch written")
        updated = updated.replace(old, new.ljust(len(old), b"\0"))
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    checked = 0
    for n in range(count):
        off = pe + 24 + optional + n * 40
        flags = struct.unpack_from("<I", data, off + 36)[0]
        if flags & 0x20000000:
            length, start = struct.unpack_from("<2I", data, off + 16)
            if data[start:start + length] != updated[start:start + length]:
                raise ValueError("Executable section changed; no patch written")
            checked += 1
    if not checked or len(updated) != len(data):
        raise ValueError("Unexpected PE layout; no patch written")
    return updated

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: patch_helper_1_0_1.py <original v1.0.0 EXE> <output EXE>")
    try:
        data = patch(Path(sys.argv[1]).read_bytes())
    except ValueError as error:
        raise SystemExit(str(error))
    Path(sys.argv[2]).write_bytes(data)
    print(hashlib.sha256(data).hexdigest())
