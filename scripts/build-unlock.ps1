param([Parameter(Mandatory=$true)][string]$Profile, [switch]$Experimental)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/toolchain.ps1"
$root = Split-Path $PSScriptRoot -Parent
$profilePath = (Resolve-Path -LiteralPath $Profile).Path
$headerPath = "$root/build/unlock/profile.h"
$options = @()
if ($Experimental) { $options = @('--experimental', '--header', $headerPath) }
& python "$root/tools/check_profile.py" $profilePath @options
if ($LASTEXITCODE -ne 0) { throw 'Profile rejected; no unlock build started.' }
# Experimental builds embed captured identity/state; both modes recheck before arming.
$buildOptions = @{}
if ($Experimental) { $buildOptions.ProfileHeader = $headerPath }
& "$root/unlock/build.ps1" @buildOptions
if ($LASTEXITCODE -ne 0) { throw 'Unlock build failed' }
New-Item -ItemType Directory -Path "$root/build/unlock" -Force | Out-Null
Copy-Item -LiteralPath "$root/unlock/build/BOOTX64.EFI" -Destination "$root/build/unlock/BOOTX64.EFI"
Copy-Item -LiteralPath $profilePath -Destination "$root/build/unlock/board.json"
Copy-Item -LiteralPath "$root/unlock/build/build-manifest.json" -Destination "$root/build/unlock/build-manifest.json"
(Get-BuildHash $profilePath) |
    Set-Content -LiteralPath "$root/build/unlock/profile-sha256.txt"
