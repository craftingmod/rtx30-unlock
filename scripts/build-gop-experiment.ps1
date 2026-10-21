param([switch]$TreeOnly,[switch]$LeafOnly,[switch]$FullFlr,[switch]$FullUnlock,[switch]$WindowsHandoff,[switch]$Production,[switch]$SsdExperiment,[switch]$ExternalProfile,[string]$Profile='')
$ErrorActionPreference = 'Stop'
if ($ExternalProfile -and (-not $SsdExperiment -or -not $Production -or -not $WindowsHandoff -or -not $FullUnlock)) { throw '-ExternalProfile requires -FullUnlock -WindowsHandoff -Production -SsdExperiment.' }
if ($SsdExperiment -and (-not $Production -or -not $WindowsHandoff -or -not $FullUnlock)) { throw '-SsdExperiment requires -FullUnlock -WindowsHandoff -Production.' }
if ($Production -and (-not $FullUnlock -or -not $WindowsHandoff)) { throw '-Production requires -FullUnlock -WindowsHandoff.' }
if ($WindowsHandoff -and -not $FullUnlock) { throw '-WindowsHandoff requires -FullUnlock.' }
$selectedModes=0
foreach ($choice in @($TreeOnly,$LeafOnly,$FullFlr,$FullUnlock)) { if ($choice) { $selectedModes++ } }
if ($selectedModes -gt 1) { throw 'Select only one experiment mode.' }
if ($FullUnlock) { $FullFlr=$true; if (-not $Profile -and -not $ExternalProfile) { throw '-FullUnlock requires -Profile or -ExternalProfile.' } }
if ($ExternalProfile -and $Profile) { throw 'Build the common EFI without -Profile; use tools/runtime_profile.py separately.' }
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
    if ($FullUnlock) {
      if (-not $ExternalProfile) {
        & python tools/check_profile.py $Profile --experimental --header build/gop-experiment/profile.h
        if ($LASTEXITCODE -ne 0) { throw 'Unlock profile rejected.' }
        Copy-Item -LiteralPath $Profile -Destination build/gop-experiment/board.json
      }
        $sources += @('experiments/gop/visual','unlock/app','unlock/app_io','unlock/ownership','unlock/sha256','unlock/checks','unlock/port','unlock/compat',
            'unlock/vendor/unlock_610','unlock/vendor/prep_610','unlock/vendor/layout_610','unlock/vendor/vbios_610','unlock/vendor/gfw_eval')
        $modeFlags += @('-DGC_FULL_UNLOCK','-DGU_GOP_EXPERIMENT','-D__KERNEL__','-DGU_UEFI_PORT',
            '-I','unlock/compat','-Wno-unused-parameter','-Wno-unused-function')
        if ($ExternalProfile) {
            $sources += 'unlock/runtime_profile'
            $modeFlags += '-DGU_RUNTIME_PROFILE_BUILD'
        } else { $modeFlags += @('-include','build/gop-experiment/profile.h') }
    }
    if ($WindowsHandoff) { $sources += 'experiments/gop/windows'; $modeFlags += '-DGC_WINDOWS_HANDOFF' }
    if ($Production) { $modeFlags += '-DGC_PRODUCTION' }
    if ($SsdExperiment) { $modeFlags += '-DGU_SSD_EXPERIMENT' }
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
        'experiments/gop/patch.h', 'experiments/gop/visual.h', 'experiments/gop/test_patch.c', 'experiments/gop/test_patch_state.c',
        'experiments/gop/test_cycle.c', 'experiments/gop/test_io.c',
        'experiments/gop/test.cmd', 'experiments/gop/README.md', 'unlock/format.h', 'unlock/runtime.h',
        'scripts/build-gop-experiment.ps1', 'scripts/toolchain.ps1', 'scripts/host-env.cmd',
        'unlock/verify_pe.py','unlock/boot_source.h'
    ) + @(Get-ChildItem deps/edk2/MdePkg/Include -Recurse -File | ForEach-Object {
        $_.FullName.Substring($root.Length+1).Replace('\','/')
    })
    # Ship both cores so the bundled host tests remain runnable in either build mode.
    $records += @('experiments/gop/cycle.c', 'experiments/gop/tree.c', 'experiments/gop/leaf.c', 'experiments/gop/full.c', 'experiments/gop/reset.c')
    if ($FullUnlock) {
        $records += @('experiments/gop/visual.h','experiments/gop/test_visual.c','experiments/gop/test_timed_flr.c','experiments/gop/test-visual.cmd','experiments/gop/test-patch-state.cmd','unlock/assets.json','gpu_family.h','tools/check_profile.py','tools/probe_profile.py','tools/gpu_family.py')
        if (-not $ExternalProfile) { $records += @('build/gop-experiment/profile.h','build/gop-experiment/board.json') }
        $records += @(Get-ChildItem unlock -Recurse -File | Where-Object {
            $_.Extension -in '.c','.h' -and $_.FullName -notlike '*\build\*'
        } | ForEach-Object { $_.FullName.Substring($root.Length+1).Replace('\','/') })
    }
    if ($WindowsHandoff) { $records += @('experiments/gop/windows.h','experiments/gop/test_windows.c','experiments/gop/test-windows.cmd','experiments/gop/windows-check.ps1','experiments/gop/test-windows-check.ps1','analysis/compute-benchmark.cu','analysis/build-benchmark.cmd','results/benchmark.json') }
    if ($Production) { $records += @('experiments/gop/test-production.cmd','experiments/gop/test_quiet_io.c') }
    if ($SsdExperiment) { $records += @('experiments/gop/test-ssd.cmd','experiments/gop/test_ssd_flow.c') }
    if ($ExternalProfile) { $records += @('tools/runtime_profile.py','tools/test_runtime_profile.py','tools/test_probe_profile.py','unlock/tests/test_runtime_profile.c','experiments/gop/test-external-profile.cmd','experiments/gop/test_external_flow.c','docs/EXTERNAL-PROFILE.md') }
    $records = @($records | Sort-Object -Unique)
    [ordered]@{
        BuiltUtc=[DateTime]::UtcNow.ToString('o'); HardwareTested=$false; WindowsHandoff=[bool]$WindowsHandoff; Production=[bool]$Production; SsdExperiment=[bool]$SsdExperiment
        ExternalProfile=[bool]$ExternalProfile
        Mode=$(if ($FullUnlock) {'full-unlock'} elseif ($FullFlr) {'full-flr'} elseif ($LeafOnly) {'leaf-member'} elseif ($TreeOnly) {'tree-read-only'} else {'cycle'})
        EfiSha256=(Get-BuildHash 'build/gop-experiment/BOOTX64.EFI')
        Sources=@($records | ForEach-Object { [ordered]@{Path=$_; Sha256=(Get-BuildHash $_)} })
    } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 build/gop-experiment/build-manifest.json
    Write-Output $(if ($ExternalProfile) {'Built v17-external-profile: PROFILE.BIN required; DEBUG.ON enables logs and manual U; hardware untested.'} elseif ($SsdExperiment) {'Built v16-ssd-selectors: USB/GPT ESP source, SS0 skip and relaxed initial selectors; hardware untested.'} elseif ($Production) {'Built v15-auto-windows: verified unlock; DEBUG.ON enables logs and manual U; hardware untested.'} elseif ($WindowsHandoff) {'Built v14-windows-handoff: v13 recovery then verified Windows chainload; hardware untested.'} elseif ($FullUnlock) {'Built v13-mode-cycle-readback: alternate/original mode cycle and direct framebuffer verification; hardware untested.'} elseif ($FullFlr) {'Built v8-virtual-gop: full GOP release, one FLR and display restoration; no compute unlock.'} elseif ($LeafOnly) {'Built v5-leaf single text-member Stop/Supported/Start experiment; GPU GOP/EDID untouched by app.'} elseif ($TreeOnly) {'Built v4-tree read-only ownership capture; no detach/reconnect or unlock/FLR core linked.'} else {'Built GOP round-trip experiment. Only driver-model detach/reconnect; no unlock/FLR core linked.'})
} finally { Pop-Location }
