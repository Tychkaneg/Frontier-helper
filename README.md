# Frontier Helper

Native camera helper for Monster Hunter Frontier Rain HD. Version **1.0.1**.

![Frontier Helper](app/preview.png)

[Download Frontier Helper 1.0.1](https://github.com/Tychkaneg/Frontier-helper/releases/download/v1.0.1/Frontier-Helper-1.0.1.zip).

Close Rain and Helper before updating, extract the ZIP and keep your existing
`setups` folder. Run `FrontierHelper.exe`, open Rain normally and click **START**
in the city. Keep both DLLs beside the EXE:

- `FrontierTargetL3Mod.dll`: injected L3 targeting adapter.
- `FrontierOrbitRain08.dll`: the matched camera core with LT/RT and FPS snapshots.

## L3 monster targeting

Press **L3** to turn the camera horizontally toward the nearest living,
rendered large monster. Each press turns once; holding L3 does not track it.
If there is no valid target, the camera keeps its current direction.
**L3 no longer recenters behind the hunter.** R3 and LB/item selection keep
their existing behavior. Right-stick orbit controls, profiles and collision
handling remain in the camera core. F8 toggles the orbit camera.

Targeting runs while the camera is enabled, Rain has focus and the current
camera/player scene is valid. Holding L3 when focus returns does not count as
a new press. The explicit Recenter button in Helper retains its existing action.

START injects the adapter into the running game. The adapter loads the matched
camera core and forwards the control protocol and FPS camera snapshot export.
No files are installed or changed in the Rain Client folder.

## Xbox LT / RT support

LT maps to Rain's existing L2 binding, and RT maps to R2. The triggers work
independently and together. Helper reads separate XInput trigger values and
updates the DirectInput joystick state returned inside the running game.
It reads the existing controller bindings and priority without modifying
Rain configuration files. Connect the controller before clicking START.

Trigger translation stays active while Helper is connected and Rain has focus.
STOP or F8 can disable the orbit camera while the input fix remains connected;
closing Helper or losing the connection stops translation. Pressed means above
30 out of 255.

## MHFZ Plus compatibility

Frontier Helper 1.0.1 supports integration with
[MHFZ Plus](https://github.com/Sushii-dev/mhfz-plus), the frame interpolation
mod for higher display FPS in Monster Hunter Frontier Z.

Camera/FPS synchronization requires a Plus build that supports Helper's
camera snapshot bridge. Helper 1.0.1 preserves the supplied FPS-sync camera
core and forwards its snapshots while keeping L3 monster targeting, LT/RT
support and your camera profiles.

Install and configure MHFZ Plus separately, launch Rain through your normal
Plus workflow, then click **START** in Helper. Keep both Helper DLLs beside
the EXE. Helper does not install Plus; snapshot support depends on the Plus
build you use.

## Rain update compatibility

Helper and the camera core check the loaded engine against 13 exact code and
address-operand checks plus 9 mapped-data checks. Targeting adds four exact
monster-layout code checks. Changed checked instructions or addresses are
rejected. Automatic checks accept matching layouts; they do not relocate hooks
or adapt arbitrary engine updates.

The adapter requires the exact native DLL shipped in this release (SHA256
`6cb45ac130a980e16113dca952be13fa59602d5a73cdb85869716a4b76b9db08`).
Use all three matched runtime files and restart Rain when updating an injected
module. Other DLL versions are rejected before the adapter installs its hook.

## Source and build

The new targeting adapter is in [app/source/monster-target](app/source/monster-target).
Use Zig 0.13.0 on Windows:

```powershell
cd app
.\build.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\test-monster-target.ps1 -Zig 'C:\Tools\zig\zig.exe'
```

`build.ps1` builds only the targeting adapter. The supplied FPS-sync camera
core is included as a binary: its updated source was not available. The other
source directories are the earlier 1.0.0 implementation. `build-legacy.ps1`
builds those earlier sources into `legacy-build-output`, without overwriting
the current release. Its output does not reproduce the FPS-sync core.

The 1.0.1 EXE changes only version/control text and the injected DLL filename.
[patch_helper_1_0_1.py](app/source/monster-target/patch_helper_1_0_1.py) reproduces
it from the exact EXE in the public v1.0.0 ZIP, verifies the input hash and
requires all executable sections to remain byte-identical.

## Verification

The package passed the 84-check GUI self-test at 96 DPI. Integration tests load
the exact packaged camera core and adapter in an isolated engine fixture.
They cover L3 targeting, small/dead filtering, press/hold/focus behavior,
no-target behavior, other buttons and both trigger bytes, profiles,
FPS snapshot forwarding, camera disable, and changed-native/engine rejection.
Fixture DLLs are kept outside the runtime package.

On 2026-10-11 the user confirmed L3 targeting in Rain, then confirmed it again
after START in the updated Helper. Runtime logs recorded targets for Farunokku,
Hypnocatrice and Rathian. This is separate from the automated fixture checks.
See [1.0.1 verification](app/verification/monster-target-1.0.1.json) and
[previous trigger verification](app/verification/triggers-1.0.0.json).
