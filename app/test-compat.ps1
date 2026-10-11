param([string]$Zig='zig', [uint32]$GamePid=0, [switch]$ControllerTests)
$ErrorActionPreference='Stop'
$taskTestRoot=$PSScriptRoot
$taskTestOutput=Join-Path $taskTestRoot 'compatibility-test-output'
New-Item -ItemType Directory -Path $taskTestOutput -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $taskTestRoot '.build-cache\global'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $taskTestRoot '.build-cache\local'
$taskCompatExe=Join-Path $taskTestOutput 'test-engine-compat.exe'
$taskCoreExe=Join-Path $taskTestOutput 'test-core.exe'
& $Zig cc -target x86-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror -Wno-unused-function (Join-Path $taskTestRoot 'source\rain-studio\tests\test_engine_compat.c') -o $taskCompatExe
if($LASTEXITCODE -ne 0){throw 'Compatibility test build failed'}
& $taskCompatExe
if($LASTEXITCODE -ne 0){throw 'Compatibility tests failed'}
& $Zig cc -target x86-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror -Wno-unused-function (Join-Path $taskTestRoot 'source\control-core\tests\test_core.c') -o $taskCoreExe
if($LASTEXITCODE -ne 0){throw 'Camera-core test build failed'}
& $taskCoreExe
if($LASTEXITCODE -ne 0){throw 'Camera-core tests failed'}
if($GamePid){
    & $taskCompatExe $GamePid
    if($LASTEXITCODE -ne 0){throw 'Running Rain engine compatibility check failed'}
}

if($ControllerTests){
    $taskTriggerExe=Join-Path $taskTestOutput 'test-triggers.exe'
    & $Zig cc -target x86-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror -Wno-unused-function (Join-Path $taskTestRoot 'source\control-core\tests\test_triggers.c') (Join-Path $taskTestRoot 'source\control-core\src\trigger_win.c') -ldinput8 -ldxguid -luser32 -o $taskTriggerExe
    if($LASTEXITCODE -ne 0){throw 'Trigger test build failed'}
    & $taskTriggerExe
    if($LASTEXITCODE -ne 0){throw 'Trigger tests failed (requires a connected Xbox/XInput controller)'}
}
