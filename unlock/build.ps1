param([string]$ProfileHeader = '')
$ErrorActionPreference = 'Stop'
$profileOptions = @('-DGU_TARGET_PCI_ID=0x220610deU')
if ($ProfileHeader) {
    $ProfileHeader = (Resolve-Path -LiteralPath $ProfileHeader).Path
    $profileOptions = @('-include', $ProfileHeader)
}
Set-Location -LiteralPath $PSScriptRoot
. "$PSScriptRoot/../scripts/toolchain.ps1"
New-Item -ItemType Directory -Path 'build' -Force | Out-Null
$sources = @('../src/unlock/app','../src/unlock/app_io','../src/unlock/ownership','../src/unlock/format','../src/unlock/runtime','../src/unlock/sha256','../src/unlock/checks','../src/unlock/port','../src/unlock/compat',
    '../src/unlock/vendor/unlock_610','../src/unlock/vendor/prep_610','../src/unlock/vendor/layout_610','../src/unlock/vendor/vbios_610','../src/unlock/vendor/gfw_eval')
$objects = @()
foreach ($source in $sources) {
    $object = 'build/' + ($source -replace '/','_') + '.obj'
    & "$llvmRoot/clang.exe" -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -Wno-unused-parameter -D__KERNEL__ -DGU_UEFI_PORT @profileOptions -I ../src/unlock/compat -I ../deps/edk2/MdePkg/Include -I ../deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}
& "$llvmRoot/lld-link.exe" /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/BOOTX64.map /out:build/BOOTX64.EFI @objects
if ($LASTEXITCODE -ne 0) { throw 'EFI link failed' }
& python verify_pe.py build/BOOTX64.EFI build/BOOTX64.map --output build/pe-verification.json
if ($LASTEXITCODE -ne 0) { throw 'PE verification failed' }
$root = Split-Path $PSScriptRoot -Parent
$inputFiles = @(
    Get-ChildItem -LiteralPath . -Recurse -File
    Get-ChildItem -LiteralPath "$root/src/unlock" -Recurse -File
)
$inputs = @($inputFiles | Where-Object {
    $_.Extension -in '.c','.h','.cmd','.ps1','.py','.psm1','.json' -and $_.FullName -notlike '*\build\*' -and $_.FullName -notlike '*\releases\*'
} | Sort-Object FullName | ForEach-Object {
    [ordered]@{ Path=$_.FullName.Substring($root.Length+1).Replace('\','/'); Sha256=(Get-BuildHash $_.FullName) }
})
[ordered]@{
    BuiltUtc=[DateTime]::UtcNow.ToString('o'); TargetPci=$(if ($ProfileHeader) {'profile'} else {'220610de'});
    Experimental=[bool]$ProfileHeader;
    ProfileHeaderSha256=$(if ($ProfileHeader) {Get-BuildHash $ProfileHeader} else {$null});
    SourceManifestSha256=(Get-BuildHash "$root/SOURCE-MANIFEST.json");
    EfiSha256=(Get-BuildHash 'build/BOOTX64.EFI');
    PeVerificationSha256=(Get-BuildHash 'build/pe-verification.json');
    Edk2Commit='c1415771ac592ad8509351c46cd3cda4f38d7d52'; Sources=$inputs;
    HardwareTested=$false
} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath 'build/build-manifest.json'
Write-Output 'Built and inspected locally. Run test-all.ps1, then tools/package_usb.py to prepare a local USB directory.'
