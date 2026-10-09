"""Generate synthetic engine bytes directly from the current production guards."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
source = (root / "src/engine_guards.h").read_text()
arrays = dict(re.findall(r"static const unsigned char e(\d+)\[\]=\{([^}]+)\};", source))
offsets = re.findall(r"base\[(0x[0-9a-fA-F]+|\d+)\+i\]&m(\d+)", source)
assert len(arrays) == len(offsets) == 13
lines = ["/* Generated from src/engine_guards.h; tests only. */",
         "typedef struct {unsigned rva, length;unsigned char bytes[64];} FixtureGuard;",
         "static const FixtureGuard fixture_guards[]={"]
for offset, ident in offsets:
    values = [int(v) for v in arrays[ident].split(",")]
    assert len(values) <= 64
    lines.append(f"{{{offset},{len(values)},{{{','.join(map(str, values))}}}}},")
lines += ["};"]
out = root / "control-core/build/fixture_guards.h"
out.parent.mkdir(exist_ok=True)
out.write_text("\n".join(lines) + "\n")
