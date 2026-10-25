<#
.SYNOPSIS
    Install the RTX30-unlock UEFI application on a hidden internal ESP-type
    partition and make it the first UEFI boot entry.

.DESCRIPTION
    Creates a small FAT32 partition of type EFI System Partition on the internal
    GPT system disk, copies the unlocker package to it, registers a UEFI boot
    entry pointing at \EFI\BOOT\BOOTX64.EFI and puts that entry first in the
    firmware boot order. The partition is then hidden (no drive letter, GPT
    no-automount attribute), like the Windows ESP.

    The original "Windows Boot Manager" entry is not modified. Prerequisite:
    Secure Boot must be disabled.

.PARAMETER PackageDir
    Folder that contains EFI\BOOT\BOOTX64.EFI and RTX-UNLOCK\ASSETS\.

.PARAMETER SizeMB
    Size of the new partition in MB. Default 512.

.PARAMETER Label
    FAT32 volume label. Default RTX30UNLOCK.

.PARAMETER SkipHide
    Keep the drive letter instead of hiding the partition (debugging).

.EXAMPLE
    .\install-internal.ps1 -PackageDir C:\rtx30-package

.NOTES
    Requires an elevated PowerShell. Reversible with remove-internal.ps1.
    Tested on Windows 11 26300, ASUS B460M-K, single NVMe GPT disk.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [int]$SizeMB = 512,
    [string]$Label = 'RTX30UNLOCK',
    [switch]$SkipHide
)

$ErrorActionPreference = 'Stop'
function Fail([string]$m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
function Info([string]$m) { Write-Host "[*] $m" }

# --- preconditions -----------------------------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Fail 'Run this script from an elevated PowerShell (Administrator).' }
$PackageDir = (Resolve-Path $PackageDir).Path
foreach ($p in @('EFI\BOOT\BOOTX64.EFI', 'RTX-UNLOCK\ASSETS')) {
    if (-not (Test-Path (Join-Path $PackageDir $p))) { Fail "package is missing: $p" }
}
if (-not (Test-Path (Join-Path $PackageDir 'RTX-UNLOCK\PROFILE.BIN'))) {
    Write-Warning 'RTX-UNLOCK\PROFILE.BIN not found. The first boot will capture one; with DEBUG.ON present it will ask for U, without it, it proceeds automatically.'
}

$sysLetter = $env:SystemDrive.TrimEnd(':')
$sysPart = Get-Partition -DriveLetter $sysLetter
$disk = Get-Disk -Number $sysPart.DiskNumber
if ($disk.PartitionStyle -ne 'GPT') { Fail 'The system disk is not GPT; UEFI boot needs GPT.' }
Info ("system disk #{0} {1} ({2:N0} GB, {3})" -f $disk.Number, $disk.FriendlyName, ($disk.Size / 1GB), $disk.PartitionStyle)

$wantBytes = [uint64]$SizeMB * 1MB
$sup = Get-PartitionSupportedSize -DriveLetter $sysLetter
$shrinkable = $sysPart.Size - $sup.SizeMin
Info ("{0}: shrinkable {1:N0} MB, requested {2} MB" -f ($sysLetter + ':'), ($shrinkable / 1MB), $SizeMB)
if ($shrinkable -lt $wantBytes) { Fail 'Not enough shrinkable free space at the end of the system volume.' }

# --- backup BCD --------------------------------------------------------------
$bcdBackup = Join-Path $PackageDir ('bcd-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bcd')
& bcdedit /export "$bcdBackup" | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'bcdedit /export failed.' }
Info "BCD exported to $bcdBackup"

# --- create partition --------------------------------------------------------
Info 'shrinking the system volume...'
Resize-Partition -DriveLetter $sysLetter -Size ($sysPart.Size - $wantBytes)
Start-Sleep -Seconds 1
$espGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$part = New-Partition -DiskNumber $disk.Number -Size $wantBytes -GptType $espGuid
Info ("created partition #{0} at offset {1}" -f $part.PartitionNumber, $part.Offset)
Start-Sleep -Seconds 1

$letter = $null
foreach ($code in 87..67) {          # W, V, U, ... fallback
    $candidate = [char]$code
    if (-not (Get-Volume -DriveLetter $candidate -ErrorAction SilentlyContinue)) { $letter = $candidate; break }
}
if (-not $letter) { Fail 'No free drive letter available.' }
Set-Partition -DiskNumber $disk.Number -PartitionNumber $part.PartitionNumber -NewDriveLetter $letter
Start-Sleep -Seconds 2
Format-Volume -DriveLetter $letter -FileSystem FAT32 -NewFileSystemLabel $Label -Confirm:$false -Force | Out-Null
Info ("formatted {0}: FAT32 '{1}'" -f $letter, $Label)

# --- copy package ------------------------------------------------------------
Info 'copying package...'
Copy-Item (Join-Path $PackageDir '*') ($letter + ':\') -Recurse -Force
foreach ($need in @('EFI\BOOT\BOOTX64.EFI', 'RTX-UNLOCK\ASSETS')) {
    if (-not (Test-Path (Join-Path ($letter + ':\') $need))) { Fail "copy verification failed: $need" }
}

# --- create UEFI boot entry --------------------------------------------------
$guid = $null
$all = (& bcdedit /enum all 2>&1 | Out-String)
if ($all -match '(?ms)identifier\s+(\{[0-9a-fA-F-]{36}\})\s+description\s+RTX30 UNLOCK') {
    $guid = $Matches[1]
    Info "reusing existing boot entry $guid"
} else {
    $out = (& bcdedit /copy '{bootmgr}' /d 'RTX30 UNLOCK' 2>&1 | Out-String)
    if ($out -match '\{[0-9a-fA-F-]{36}\}') { $guid = $Matches[0] } else { Fail "cannot create boot entry: $out" }
    Info "created boot entry $guid"
}
& bcdedit /set $guid device ("partition=" + $letter + ':') | Out-Null
& bcdedit /set $guid path '\EFI\BOOT\BOOTX64.EFI' | Out-Null
& bcdedit /set $guid description 'RTX30 UNLOCK' | Out-Null
& bcdedit /set '{fwbootmgr}' displayorder $guid /addfirst | Out-Null
Info 'boot entry added first in the firmware boot order'

# --- hide partition ----------------------------------------------------------
if (-not $SkipHide) {
    Remove-PartitionAccessPath -DiskNumber $disk.Number -PartitionNumber $part.PartitionNumber -AccessPath ($letter + ':\')
    $dp = Join-Path $env:TEMP 'rtx30-hide.dp'
    @("select disk $($disk.Number)", "select partition $($part.PartitionNumber)", 'gpt attributes=0x8000000000000000', 'exit') -join "`r`n" | Set-Content -Path $dp -Encoding ascii
    & diskpart /s "$dp" | Out-Null
    Remove-Item $dp -Force -ErrorAction SilentlyContinue
    Info 'drive letter removed, GPT no-automount attribute set (partition is hidden)'
} else {
    Write-Warning "Partition keeps drive letter $letter`: (SkipHide)."
}

Write-Host ''
Write-Host 'Installed.' -ForegroundColor Green
Write-Host ("  boot entry : RTX30 UNLOCK  {0}" -f $guid)
Write-Host ("  partition  : disk {0} partition {1} ({2} MB, ESP-type FAT32)" -f $disk.Number, $part.PartitionNumber, $SizeMB)
Write-Host ("  BCD backup : {0}" -f $bcdBackup)
Write-Host ''
Write-Host 'Next: reboot (full shutdown) - the unlocker runs first, then Windows.'
Write-Host ("Rollback: .\remove-internal.ps1 -EntryGuid {0}" -f $guid)
