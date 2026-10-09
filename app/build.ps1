param([string]$Zig='D:\MHFZZ\RAIN\orbit-research\toolchain\zig-windows-x86_64-0.13.0\zig.exe')
$ErrorActionPreference='Stop'
$taskBuildRoot=$PSScriptRoot
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $taskBuildRoot '.build-cache\global'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $taskBuildRoot '.build-cache\local'
Push-Location (Join-Path $taskBuildRoot 'source\rain-studio')
try {
    & $Zig c++ -target x86-windows-gnu -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -c src/helper_renderer.cpp -o (Join-Path $taskBuildRoot '.build-cache\renderer.obj')
    if($LASTEXITCODE -ne 0){throw "Renderer build failed: $LASTEXITCODE"}
    & $Zig cc -target x86-windows-gnu -O2 -Wall -Wextra -Werror -Wno-unused-function '-Wl,--subsystem,windows' src/helper_gui.c src/studio_backend.c src/profile_store.c (Join-Path $taskBuildRoot '.build-cache\renderer.obj') -ld2d1 -ldwrite -ldwmapi -luser32 -lgdi32 -ladvapi32 -lcomdlg32 -lshell32 -o (Join-Path $taskBuildRoot 'FrontierHelper.exe')
    if($LASTEXITCODE -ne 0){throw "Build failed: $LASTEXITCODE"}
} finally {Pop-Location}
