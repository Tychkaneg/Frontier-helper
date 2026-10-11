# L3 targeting adapter (1.0.1)

`helper_injection_adapter.c` is the production source for
`FrontierTargetL3Mod.dll`. It wraps the exact supplied FPS-sync camera core;
it does not rebuild or substitute the older camera implementation.
`native_layout.h` defines the verified private x86 ABI. The native hash guard
must match before these offsets are used. Monster code guards remain required.

The adapter selects the nearest living/rendered large monster on an L3 edge,
sets the native desired yaw, and masks L3 only in the native camera reader.
It leaves other button and trigger bytes intact and delegates snapshots to
the original native module. Missing targets keep yaw unchanged. The explicit
Helper Recenter command is unaffected. The production module can also reuse
the exact earlier live-test addon if it is already installed and owns both
hooks; that addon is optional and is not needed on a fresh Rain session.

Run `app/build.ps1` to build the adapter, or `app/test-monster-target.ps1` for
isolated tests. Fake XInput/D3DX DLLs are test-only and must never be copied
into a game or runtime package. The test engine and focus stubs exist only
inside the fixture process.

The GUI patch script accepts only the public v1.0.0 EXE hash, changes DLL-name,
version and L3 text, and verifies every executable PE section stays identical.
The updated native source is unavailable; other source directories in this
repository are the earlier 1.0.0 source snapshot.
