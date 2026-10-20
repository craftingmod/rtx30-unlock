param([switch]$TreeOnly,[switch]$LeafOnly,[switch]$FullFlr)
$ErrorActionPreference = 'Stop'
$selectedModes=0
foreach ($choice in @($TreeOnly,$LeafOnly,$FullFlr)) { if ($choice) { $selectedModes++ } }
if ($selectedModes -gt 1) { throw 'Select only one experiment mode.' }
. "$PSScriptRoot/toolchain.ps1"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Path build/gop-experiment -Force | Out-Null
    $core = if ($FullFlr) { 'experiments/gop/full' } elseif ($LeafOnly) { 'experiments/gop/leaf' } elseif ($TreeOnly) { 'experiments/gop/tree' } else { 'experiments/gop/cycle' }
    $sources = @('experiments/gop/app', $core, 'unlock/runtime', 'unlock/format')
    if ($FullFlr) { $sources += @('experiments/gop/leaf','experiments/gop/reset') }
    $modeFlags = @()
    if ($TreeOnly) { $modeFlags = @('-DGC_TREE_ONLY') }
    if ($LeafOnly) { $modeFlags = @('-DGC_LEAF_ONLY') }
    if ($FullFlr) { $modeFlags = @('-DGC_FULL_FLR') }
    $objects = @()
    foreach ($source in $sources) {
        $object = 'build/gop-experiment/' + ($source -replace '/', '_') + '.obj'
        & "$llvmRoot/clang.exe" -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror @modeFlags -I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
        if ($LASTEXITCODE -ne 0) { throw "GOP experiment compile failed: $source" }
        $objects += $object
    }
    & "$llvmRoot/lld-link.exe" /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/gop-experiment/BOOTX64.map /out:build/gop-experiment/BOOTX64.EFI @objects
    if ($LASTEXITCODE -ne 0) { throw 'GOP experiment link failed' }
    & python unlock/verify_pe.py build/gop-experiment/BOOTX64.EFI build/gop-experiment/BOOTX64.map --output build/gop-experiment/pe-verification.json
    if ($LASTEXITCODE -ne 0) { throw 'GOP experiment PE verification failed' }
    if ($TreeOnly -and (Select-String -Path build/gop-experiment/BOOTX64.map -Pattern '\bGcCycle\b|\bGuFlr\b|\bGcClaimController\b')) {
        throw 'Unexpected driver-changing core in read-only image'
    }
    if ($LeafOnly -and (Select-String -Path build/gop-experiment/BOOTX64.map -Pattern '\bGcCycle\b|\bGuFlr\b|\bGuClaimController\b')) {
        throw 'Unexpected full-GPU transaction core in leaf image'
    }
    $records = @($sources | ForEach-Object { "$_.c" }) + @(
        'experiments/gop/cycle.h', 'experiments/gop/tree.h', 'experiments/gop/test_tree.c',
        'experiments/gop/leaf.h', 'experiments/gop/test_leaf.c',
        'experiments/gop/full.h', 'experiments/gop/reset.h', 'experiments/gop/test_full.c',
        'experiments/gop/test_cycle.c', 'experiments/gop/test_io.c',
        'experiments/gop/test.cmd', 'experiments/gop/README.md', 'unlock/format.h', 'unlock/runtime.h',
        'scripts/build-gop-experiment.ps1', 'scripts/toolchain.ps1', 'scripts/host-env.cmd',
        'unlock/verify_pe.py'
    ) + @(Get-ChildItem deps/edk2/MdePkg/Include -Recurse -File | ForEach-Object {
        $_.FullName.Substring($root.Length+1).Replace('\','/')
    })
    # Ship both cores so the bundled host tests remain runnable in either build mode.
    $records += @('experiments/gop/cycle.c', 'experiments/gop/tree.c', 'experiments/gop/leaf.c', 'experiments/gop/full.c', 'experiments/gop/reset.c')
    $records = @($records | Sort-Object -Unique)
    [ordered]@{
        BuiltUtc=[DateTime]::UtcNow.ToString('o'); HardwareTested=$false
        Mode=$(if ($FullFlr) {'full-flr'} elseif ($LeafOnly) {'leaf-member'} elseif ($TreeOnly) {'tree-read-only'} else {'cycle'})
        EfiSha256=(Get-BuildHash 'build/gop-experiment/BOOTX64.EFI')
        Sources=@($records | ForEach-Object { [ordered]@{Path=$_; Sha256=(Get-BuildHash $_)} })
    } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 build/gop-experiment/build-manifest.json
    Write-Output $(if ($FullFlr) {'Built v8-virtual-gop: full GOP release, one FLR and display restoration; no compute unlock.'} elseif ($LeafOnly) {'Built v5-leaf single text-member Stop/Supported/Start experiment; GPU GOP/EDID untouched by app.'} elseif ($TreeOnly) {'Built v4-tree read-only ownership capture; no detach/reconnect or unlock/FLR core linked.'} else {'Built GOP round-trip experiment. Only driver-model detach/reconnect; no unlock/FLR core linked.'})
} finally { Pop-Location }
