FRONTIER HELPER 1.0.1 - RAIN EDITION

Close Rain and Helper before updating. Extract this ZIP into the Helper
folder and keep your existing setups folder with saved profiles.
Run FrontierHelper.exe. Keep BOTH DLLs beside it:
  FrontierTargetL3Mod.dll
  FrontierOrbitRain08.dll
Open Rain normally, enter the city and click START.
Helper injects its adapter at runtime. Rain Client files are not modified.
Restart Rain when updating DLLs already loaded by an older Helper.

CAMERA
Right stick: rotate the orbit camera.
L3: turn once toward the nearest living, rendered large monster.
No valid target: leave the camera direction unchanged.
Holding L3 does not continuously track the monster.
L3 no longer turns behind the hunter. R3 and LB/item selection are unchanged.
Targeting requires the camera enabled, Rain focused and a valid active scene.
F8: toggle the orbit camera. STOP disables it.
The explicit Recenter button keeps its existing behavior.

Drag a slider or click its number to enter a value. Enter applies it;
Esc cancels. Advanced settings contain deadzone and pitch limits.
Save profile stores your current settings. Existing schema-1 profiles work.
The portable setups folder contains saved profiles.
Tab / Shift+Tab moves between controls. Arrows adjust sliders.
Ctrl+S saves a profile. Ctrl+R restores defaults.

LT / RT AND FPS
LT maps to L2; RT maps to R2, independently or simultaneously.
Connect the controller before START. Runtime DirectInput translation uses
Rain's existing controller bindings and leaves its files unchanged.
The trigger threshold is above 30/255. Translation requires the Helper
connection and Rain focus. F8/STOP can disable the camera while triggers
remain connected; closing Helper or losing the link stops translation.
The original supplied FPS-sync core is retained byte-for-byte. Its module
name and 60-byte camera snapshots are preserved for compatible Frontier Plus.
This package does not install or replace Frontier Plus.

COMPATIBILITY
Use the matched EXE and both DLLs from this release. The adapter checks the
exact packaged camera-core SHA256 before loading it. Camera and targeting
code/address checks must match the loaded Rain engine; incompatible layouts
are rejected. Automatic matching does not adapt arbitrary future updates.

VERIFICATION AND SOURCE
The 1.0.1 GUI passed 84 checks at 96 DPI. Isolated integration tests passed
for targeting, filtering, no-target/hold/focus behavior, other input bytes,
profiles, FPS snapshot forwarding and compatibility rejection.
The user confirmed L3 targeting in Rain after START in the updated Helper.
The release EXE changes only strings, with executable sections unchanged.

The GitHub repository includes targeting-adapter source and a guarded EXE
patch script. The supplied updated FPS-sync native core is binary-only;
the other source folders describe the older 1.0.0 implementation.
Do not rebuild an old camera core over the packaged FPS-sync DLL.
SHA256 values for runtime files and this README are in package-manifest.json.
