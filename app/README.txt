FRONTIER HELPER 0.10.1 — RAIN EDITION

Run FrontierHelper.exe. No installation is required.
Keep FrontierOrbitRain08.dll next to the executable.

Open Rain HD normally and enter the city. Click START to connect.
STOP restores the stock camera. Closing Helper also disables its camera.
RAIN UPDATE COMPATIBILITY
Helper checks the loaded HD engine instead of requiring one client.dll hash.
Updates with the same verified camera/collision instructions and address
layout are accepted automatically. SHA256 is logged for diagnostics only.
Both Helper and its camera DLL check 13 exact code blocks, including address
operands, and 9 mapped-data ranges before installing the camera hook.
The hook still requires the original D3DXMatrixLookAtRH import.
Changed instructions, moved addresses or inaccessible engine memory are
rejected. This does not relocate or adapt arbitrary future engine changes.
Tested in Rain HD 20261009170842_c176749 on 2026-10-10.

CAMERA
Drag a slider or click its number to enter an exact value.
Enter applies the value; Esc cancels editing. Invalid values are rejected.
Advanced settings contains deadzone, pitch limits and reset tilt.
Live preview controls camera updates while Helper has focus.
Reset restores default settings. Save profile saves the current values.

PROFILES
The dropdown and Profiles page load local camera profiles.
Modified values are marked. Loading another profile asks before discarding
unsaved changes. Use Save profile and a new name to create a copy.
Using an existing name updates that profile.
Import and Export use the existing schema-1 JSON format.
The setups folder contains your profiles; it is portable with the app.

FILES
Select the Rain executable when automatic detection needs a specific path.
Open the profiles folder, view/export the connection log, choose Auto or
Gamepad 1–4, and use Recenter for the connected camera.
F8 toggles the camera in game. L3 recenters it.

KEYBOARD
Tab / Shift+Tab: move between controls.
Arrow keys: adjust the focused slider; Shift uses larger steps.
Home / End: slider limits. Enter: exact value or activate a button.
Enter / Esc: apply / cancel numeric editing.
Dropdown: Up / Down, Home / End, Enter to load, Esc to close.
Mouse wheel / Page Up / Page Down: scroll content.
Ctrl+S: Save profile. Ctrl+R: restore defaults.

VERIFICATION
Native UI model and rendering checks passed at 100%, 125% and 150% DPI.
84 checks at each scale, plus JSON profile codec tests.
Compatibility tests passed for a valid engine layout and rejection of
changed code/address operands, non-executable code, inaccessible data and
an incorrect engine base. Camera-core tests include 5,000 geometry cases.
Live checks passed for engine verification, injection, channel ownership,
settings, enable/status/disable. The user confirmed the updated camera
works in Rain; the attach journal also recorded active camera/settings.
The UI/DPI results above are from 0.10.0; 0.10.1 changes compatibility and
the displayed version, with the existing UI controls retained.

Source, build.ps1 and test-compat.ps1 are available in the GitHub repository.
The portable ZIP includes runtime files, this README and a hash manifest.
Your existing setups folder can be kept when updating the EXE and DLL.
