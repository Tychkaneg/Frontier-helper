param([string]$Zig='zig', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$taskBuildRoot=$PSScriptRoot
$taskNative=Join-Path $taskBuildRoot 'FrontierOrbitRain08.dll'
$taskNativeHash='6cb45ac130a980e16113dca952be13fa59602d5a73cdb85869716a4b76b9db08'
if((Get-FileHash -Algorithm SHA256 -LiteralPath $taskNative).Hash.ToLowerInvariant() -ne $taskNativeHash){
    throw 'The adapter requires the exact packaged FPS-sync native DLL.'
}
if(!$OutputDirectory){$OutputDirectory=$taskBuildRoot}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskOutput=Join-Path (Resolve-Path -LiteralPath $OutputDirectory).Path 'FrontierTargetL3Mod.dll'
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $taskBuildRoot '.build-cache\global'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $taskBuildRoot '.build-cache\local'
& $Zig cc -target x86-windows-gnu -std=c11 -shared -O2 -Wall -Wextra -Werror -Wno-unused-function (Join-Path $taskBuildRoot 'source\monster-target\helper_injection_adapter.c') -luser32 -ladvapi32 '-Wl,--image-base,0x62000000' -o $taskOutput
if($LASTEXITCODE -ne 0){throw "Target adapter build failed: $LASTEXITCODE"}
Write-Output $taskOutput
