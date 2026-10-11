param([string]$Zig='zig')
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskOutput=Join-Path $taskRoot 'monster-target-test-output'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $taskRoot '.build-cache\global'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $taskRoot '.build-cache\local'
$taskSource=Join-Path $taskRoot 'source\monster-target'
$taskCoreTests=Join-Path $taskRoot 'source\control-core\tests'
# Test-only fake DLLs stay in this ignored fixture directory, never app or Client.
& $Zig cc -target x86-windows-gnu -shared -O2 -Wall -Wextra -Werror (Join-Path $taskSource 'fake_xinput.c') (Join-Path $taskSource 'fake_xinput.def') -o (Join-Path $taskOutput 'xinput1_4.dll')
if($LASTEXITCODE -ne 0){throw 'Fake XInput build failed'}
& $Zig cc -target x86-windows-gnu -shared -O2 -Wall -Wextra -Werror (Join-Path $taskCoreTests 'fake_d3dx.c') (Join-Path $taskCoreTests 'fake_d3dx.def') -o (Join-Path $taskOutput 'd3dx9_43.dll')
if($LASTEXITCODE -ne 0){throw 'Fake D3DX build failed'}
& $Zig cc -target x86-windows-gnu -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror (Join-Path $taskSource 'test_monster_target.c') -o (Join-Path $taskOutput 'test-monster-target.exe')
if($LASTEXITCODE -ne 0){throw 'Target math test build failed'}
& $Zig cc -target x86-windows-gnu -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror -Wno-unused-function (Join-Path $taskSource 'test_helper_injection.c') -luser32 -ladvapi32 -o (Join-Path $taskOutput 'test-helper-injection.exe')
if($LASTEXITCODE -ne 0){throw 'Injection integration test build failed'}
Copy-Item -LiteralPath (Join-Path $taskRoot 'FrontierOrbitRain08.dll') -Destination $taskOutput -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'FrontierTargetL3Mod.dll') -Destination $taskOutput -Force
Push-Location $taskOutput
try {
    & '.\test-monster-target.exe'
    if($LASTEXITCODE -ne 0){throw 'Target math tests failed'}
    & '.\test-helper-injection.exe'
    if($LASTEXITCODE -ne 0){throw 'Injection integration failed'}
    & '.\test-helper-injection.exe' '--bad-engine'
    if($LASTEXITCODE -ne 0){throw 'Engine guard rejection failed'}
    & '.\test-helper-injection.exe' '--bad-monster'
    if($LASTEXITCODE -ne 0){throw 'Monster guard rejection failed'}
} finally {Pop-Location}
$taskBadOutput=Join-Path $taskOutput 'bad-hash'
New-Item -ItemType Directory -Path $taskBadOutput -Force | Out-Null
foreach($taskFile in @('test-helper-injection.exe','FrontierTargetL3Mod.dll','FrontierOrbitRain08.dll','xinput1_4.dll','d3dx9_43.dll')){
    Copy-Item -LiteralPath (Join-Path $taskOutput $taskFile) -Destination $taskBadOutput -Force
}
# Corrupt only the fixture COPY to verify that the native hash guard rejects it.
$taskBadNative=Join-Path $taskBadOutput 'FrontierOrbitRain08.dll'
$taskBytes=[System.IO.File]::ReadAllBytes($taskBadNative)
$taskBytes[$taskBytes.Length-1]=$taskBytes[$taskBytes.Length-1] -bxor 1
[System.IO.File]::WriteAllBytes($taskBadNative,$taskBytes)
Push-Location $taskBadOutput
try {
    & '.\test-helper-injection.exe' '--bad-hash'
    if($LASTEXITCODE -ne 0){throw 'Native hash rejection failed'}
} finally {Pop-Location}
