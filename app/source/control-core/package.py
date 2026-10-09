"""Pack the reusable core, dependencies and production DLL; never pack fake DLLs."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import hashlib
import json

core = Path(__file__).resolve().parent
repo = core.parent
out = repo.parent / "Frontier-Orbit-Control-Core-Preview.zip"
files = [p for p in core.rglob("*") if p.is_file()
         and "build" not in p.relative_to(core).parts
         and "__pycache__" not in p.parts]
files.append(core / "build/FrontierOrbitControl.dll")
files += [repo / "src" / name for name in
          ("orbit_math.h", "orbit_session.h", "collision_math.h", "engine_guards.h")]
files.append(repo / "docs/external-camera-app-plan.md")
manifest = {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(files)}
with ZipFile(out, "w", ZIP_DEFLATED) as archive:
    for p in sorted(files):
        archive.write(p, "frontier-orbit/" + str(p.relative_to(repo)))
    archive.writestr("frontier-orbit/control-core/SHA256.json",
                     json.dumps(manifest, indent=2) + "\n")
with ZipFile(out) as archive:
    assert archive.testzip() is None
    dlls = [name for name in archive.namelist() if name.lower().endswith(".dll")]
    assert dlls == ["frontier-orbit/control-core/build/FrontierOrbitControl.dll"]
print(f"{out}: {out.stat().st_size} bytes; production DLL only, sources and checksums")
