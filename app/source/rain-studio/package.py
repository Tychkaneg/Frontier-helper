"""Package the two production binaries and auditable source; never game files."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import struct
import zipfile

studio = Path(__file__).resolve().parent
repo = studio.parent
output = repo.parent / "Frontier-Orbit-Studio-Rain-0.8.1.zip"
prefix = "Frontier-Orbit-Studio-Rain-0.8.1/"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def pe_info(path):
    data = path.read_bytes()
    assert data[:2] == b"MZ"
    header = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[header:header + 4] == b"PE\0\0"
    assert struct.unpack_from("<H", data, header + 4)[0] == 0x14C
    optional = header + 24
    assert struct.unpack_from("<H", data, optional)[0] == 0x10B
    return {"machine": "PE32 x86", "bytes": len(data), "sha256": sha(path),
            "subsystem": struct.unpack_from("<H", data, optional + 68)[0]}

files = {}
for name in ["FrontierOrbitStudio.exe", "FrontierOrbitRain08.dll"]:
    files[name] = studio / "build" / name
for name in ["README.txt", "REPORT.md", "preview.png", "setups-preview.png"]:
    files[name] = studio / name

for subtree in ["src", "tests", "fork-0.7/source"]:
    for path in sorted((studio / subtree).rglob("*")):
        if path.is_file() and path.suffix in {".c", ".h", ".py", ".def", ".txt"}:
            files["source/rain-studio/" + path.relative_to(studio).as_posix()] = path
for name in ["Makefile", "README.txt", "REPORT.md", "package.py", "smoke-verification.json", "preview.png", "setups-preview.png"]:
    files["source/rain-studio/" + name] = studio / name
for name in ["README.txt", "REPORT.md", "verification.json"]:
    files["source/rain-studio/fork-0.7/" + name] = studio / "fork-0.7" / name

core = repo / "control-core"
for path in sorted(core.rglob("*")):
    if "build" in path.relative_to(core).parts or not path.is_file():
        continue
    if path.name == "Makefile" or path.suffix in {".c", ".h", ".def", ".py", ".md", ".txt", ".json"}:
        files["source/control-core/" + path.relative_to(core).as_posix()] = path
for name in ["orbit_math.h", "orbit_session.h", "collision_math.h", "engine_guards.h",
             "test_orbit.c", "test_session.c", "test_collision.c"]:
    files["source/src/" + name] = repo / "src" / name
files["source/docs/external-camera-app-plan.md"] = repo / "docs/external-camera-app-plan.md"

smoke = json.loads((studio / "smoke-verification.json").read_text())
assert smoke["save_same_name_overwrites"] and smoke["game_exit_disconnects"]
assert not smoke["real_rain_gameplay_test"]
manifest = {
    "product": "Frontier Orbit Studio 0.8.1 — Rain Edition",
    "date_utc": datetime.now(timezone.utc).isoformat(),
    "supported_client": "Rain 20260929141936_cb31ac5 HD / client.exe",
    "supported_client_dll_sha256": "ec8d0f6eb499fd58a287c73ca396cf49984cf42fd88ad60863f1b5c9756f215b",
    "engine": {"manually_mapped_base": "0x10000000", "code_guards": 13},
    "runtime": {name: pe_info(studio / "build" / name)
                for name in ["FrontierOrbitStudio.exe", "FrontierOrbitRain08.dll"]},
    "build": {"compiler": "Zig 0.13.0", "warnings_as_errors": True,
              "gui": "Win32 / GDI / C", "dotnet_python_browser_required": False},
    "profile_codec_tests": {"portable": "passed", "asan_ubsan": "passed"},
    "smoke_test": smoke,
    "attach_investigation": {"reported_rain_message": "Detected an invalid application",
                             "reported_game_exits": True, "old_07_works_per_user": True,
                             "message_root_cause_confirmed": False,
                             "fix": "Close the temporary VM_WRITE/CREATE_THREAD handle before IPC; no loader handle on reuse",
                             "native_dll_identical_to_08": sha(studio / "build/FrontierOrbitRain08.dll") ==
                                 "f7fd3a43730c3963960e2bea4a9a6efe491a903938c89efd84e2108d8c84b43d"},
    "manual_start_only": True, "game_files_modified": False, "server_modified": False,
    "source_sha256": {name: sha(path) for name, path in files.items() if name.startswith("source/")},
    "baseline_core_evidence": "source/control-core/verification.json (earlier core-only experiment)",
}
assert manifest["runtime"]["FrontierOrbitStudio.exe"]["subsystem"] == 2
(studio / "verification.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
files["verification.json"] = studio / "verification.json"

with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name, path in sorted(files.items()):
        archive.writestr(prefix + name, path.read_bytes())
    sums = "".join(sha(path) + "  " + name + "\n" for name, path in sorted(files.items()))
    archive.writestr(prefix + "SHA256SUMS.txt", sums)
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    binaries = [Path(name).name for name in archive.namelist() if name.endswith((".exe", ".dll"))]
    assert sorted(binaries) == ["FrontierOrbitRain08.dll", "FrontierOrbitStudio.exe"]
print(json.dumps({"archive": str(output), "bytes": output.stat().st_size,
                  "sha256": sha(output), "runtime": manifest["runtime"]}, indent=2))
