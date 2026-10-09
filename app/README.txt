FRONTIER HELPER 0.10.0 — RAIN EDITION

Run FrontierHelper.exe. No installation is required.
Keep FrontierOrbitRain08.dll next to the executable.

Open Rain HD normally and enter the city. Click START to connect.
STOP restores the stock camera. Closing Helper also disables its camera.
The supported Rain engine remains 20260929141936_cb31ac5 (HD).

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
The Rain DLL is byte-identical to the previous working version.
Live in-game acceptance has not been tested in this run.

Source and build.ps1 are available in the local development folder.
The portable ZIP includes only runtime files, this README and profiles.
