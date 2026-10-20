$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/toolchain.ps1"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Path build/probe -Force | Out-Null
    $objects = @()
    foreach ($source in @('probe_app', 'probe_core', 'efi_runtime', 'unlock/sha256')) {
        $object = 'build/probe/' + ($source -replace '/', '_') + '.obj'
        & "$llvmRoot/clang.exe" -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
        if ($LASTEXITCODE -ne 0) { throw "Probe compile failed: $source" }
        $objects += $object
    }
    & "$llvmRoot/lld-link.exe" /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/probe/BOOTX64.map /out:build/probe/BOOTX64.EFI @objects
    if ($LASTEXITCODE -ne 0) { throw 'Probe link failed' }
    & python unlock/verify_pe.py build/probe/BOOTX64.EFI build/probe/BOOTX64.map --output build/probe/pe-verification.json
    if ($LASTEXITCODE -ne 0) { throw 'Probe PE verification failed' }
    Write-Output 'Built build/probe/BOOTX64.EFI (read-only GPU diagnostics; no unlock code linked).'
} finally { Pop-Location }
