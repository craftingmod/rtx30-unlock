<#
.SYNOPSIS
    Remove the "RTX30 UNLOCK" UEFI boot entry created by install-internal.ps1.

.DESCRIPTION
    Deletes the firmware boot entry. With -DeletePartition it also deletes the
    hidden ESP-type FAT32 partition that holds the unlocker (the freed space is
    left unallocated; extend C: in Disk Management if you want it back).

.PARAMETER EntryGuid
    GUID of the boot entry, printed by install-internal.ps1.

.PARAMETER DeletePartition
    Also delete the package partition. Locate it by volume label.

.PARAMETER VolumeLabel
    FAT32 label of the partition to delete. Default RTX30UNLOCK.

.NOTES
    Requires an elevated PowerShell.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EntryGuid,
    [switch]$DeletePartition,
    [string]$VolumeLabel = 'RTX30UNLOCK'
)

$ErrorActionPreference = 'Stop'
function Fail([string]$m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
function Info([string]$m) { Write-Host "[*] $m" }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Fail 'Run this script from an elevated PowerShell (Administrator).' }

& bcdedit /delete "$EntryGuid" /f | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Warning "bcdedit /delete $EntryGuid reported an error (entry may not exist)." }
else { Info "boot entry $EntryGuid deleted" }

if ($DeletePartition) {
    $targets = @()
    foreach ($d in Get-Disk | Where-Object { $_.PartitionStyle -eq 'GPT' -and -not $_.IsBoot }) {
        foreach ($p in Get-Partition -DiskNumber $d.Number -ErrorAction SilentlyContinue) {
            $guidPath = $p.AccessPaths | Where-Object { $_ -like '\\?\Volume{*}\' } | Select-Object -First 1
            if (-not $guidPath) { continue }
            $label = (Get-Volume -UniqueId $guidPath -ErrorAction SilentlyContinue).FileSystemLabel
            if ($label -eq $VolumeLabel) { $targets += $p }
        }
    }
    if ($targets.Count -ne 1) { Fail ("could not uniquely locate a GPT partition labelled '$VolumeLabel' (found {0})" -f $targets.Count) }
    $t = $targets[0]
    Write-Warning ("About to DELETE partition #{0} on disk {1} ({2:N0} MB, label '{3}')." -f $t.PartitionNumber, $t.DiskNumber, ($t.Size / 1MB), $VolumeLabel)
    Remove-Partition -DiskNumber $t.DiskNumber -PartitionNumber $t.PartitionNumber -Confirm:$false
    Info 'partition deleted; the space is now unallocated (extend C: manually if desired)'
}

Write-Host 'Done. Boot entries can be verified with: bcdedit /enum {fwbootmgr}' -ForegroundColor Green
