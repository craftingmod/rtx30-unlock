$ErrorActionPreference = 'Stop'
$clang = Get-Command clang.exe -ErrorAction SilentlyContinue
$llvmRoot = if ($env:LLVM_BIN) { $env:LLVM_BIN } elseif ($clang) { Split-Path $clang.Source } else {
    'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/Llvm/x64/bin'
}
foreach ($tool in @('clang.exe', 'lld-link.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $llvmRoot $tool))) { throw "Missing $tool in $llvmRoot. Set LLVM_BIN." }
}

function Get-BuildHash([string]$Path) {
    $stream = [System.IO.File]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '') }
    finally { $hash.Dispose(); $stream.Dispose() }
}
