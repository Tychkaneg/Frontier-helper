# Frontier Helper

Native camera helper for Monster Hunter Frontier Rain HD. Version **0.10.1**.

![Frontier Helper](app/preview.png)

[Download Frontier Helper 0.10.1](https://github.com/Tychkaneg/Frontier-helper/releases/download/v0.10.1/Frontier-Helper-0.10.1.zip).
Extract the ZIP and run `FrontierHelper.exe`. Keep `FrontierOrbitRain08.dll`
next to it. Open Rain normally, enter the city, then click **START**.
When updating, close Helper and keep your existing `setups` folder.

## Rain update compatibility

0.10.1 fixes the rejection of Rain HD `20261009170842_c176749` after
`client.dll` changed. File SHA256 is now diagnostic; compatibility is checked
against the loaded engine in both Helper and the injected camera module.

Updates are accepted automatically when the known camera/collision layout
passes 13 exact code and address-operand checks plus 9 mapped-data checks.
The original camera import, process identity and local control-channel owner
are also checked before enabling the camera.

Changes to checked code or addresses are rejected. Automatic compatibility
does not relocate hooks or adapt arbitrary engine changes.

## Build and verification

Use Zig 0.13.0 on Windows, with `zig` on PATH or pass its location:

```powershell
cd app
.\build.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\test-compat.ps1 -Zig 'C:\Tools\zig\zig.exe'
```

The build produces both the application and its Rain camera DLL.
Tests cover compatibility acceptance/rejection and camera-core geometry.
Optional `-GamePid <pid>` adds a read-only check of a running Rain engine.

Live engine verification, injection, settings and enable/status/disable
passed on the updated Rain build. In-game camera operation was confirmed
by the user. See [verification](app/verification/compatibility-0.10.1.json).
