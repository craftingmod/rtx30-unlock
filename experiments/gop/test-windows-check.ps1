$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('../../build/gop-experiment/windows-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
function Get-CimInstance($ClassName) {
    if ($ClassName -eq 'Win32_OperatingSystem') { return [pscustomobject]@{LastBootUpTime=[datetime]'2026-10-21T01:00:00'} }
    return [pscustomobject]@{Name='fixture';DriverVersion='test';PNPDeviceID='test';Status='OK';ConfigManagerErrorCode=0}
}
function Get-Command($Name) { if ($Name -ne 'nvidia-smi.exe') { throw 'Unexpected command lookup' } }
& "$PSScriptRoot/windows-check.ps1" -Output (Join-Path $fixture 'no-benchmark')
$summary = Get-Content -Raw (Join-Path $fixture 'no-benchmark/summary.json') | ConvertFrom-Json
if ($summary.BenchmarkState -ne 'not-run') { throw 'Missing benchmark was reported as run' }
$valid = '{"gpu":"NVIDIA GeForce RTX 3080","pci":"0000:01:00.0","benchmarks":[{"reference_checked":true,"gflops":1},{"reference_checked":true,"gflops":2},{"reference_checked":true,"gflops":3}]}'
foreach ($case in @('valid','3090ti','wrong-pci','bad-result','failed','invalid')) {
    $fake = Join-Path $fixture ($case + '.cmd')
    $payload = switch ($case) {
        '3090ti' { $valid.Replace('RTX 3080','RTX 3090 Ti') }
        'wrong-pci' { $valid.Replace('0000:01:00.0','0000:02:00.0') }
        'bad-result' { $valid.Replace('"reference_checked":true','"reference_checked":false') }
        default { $valid }
    }
    $body = if ($case -eq 'failed') { "@exit /b 3" } elseif ($case -eq 'invalid') { "@echo invalid`r`n@exit /b 0" } else { "@echo $payload`r`n@exit /b 0" }
    Set-Content -Encoding Ascii -Path $fake -Value $body
    & "$PSScriptRoot/windows-check.ps1" -Benchmark $fake -Output (Join-Path $fixture $case)
    $summary = Get-Content -Raw (Join-Path $fixture "$case/summary.json") | ConvertFrom-Json
    $expected = if ($case -in @('valid','3090ti')) {'passed'} else {'failed'}
    if ($summary.BenchmarkState -ne $expected -or -not $summary.BenchmarkSha256) { throw "Wrong result for $case" }
}
Write-Output 'PASS 7 Windows result collection scenarios; device queries and benchmark processes simulated'
