param([string]$Benchmark='', [string]$Output='')
$ErrorActionPreference = 'Stop'
if (-not $Output) { $Output = Join-Path $PSScriptRoot ('RESULTS-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $Output) { throw 'Choose a new results directory.' }
New-Item -ItemType Directory -Path $Output | Out-Null
$summary = [ordered]@{ CollectedAt=(Get-Date).ToString('o'); Computer=$env:COMPUTERNAME; LastBootUpTime=$null; NvidiaSmiExitCode=$null; BenchmarkExitCode=$null; BenchmarkSha256=$null; BenchmarkState='not-run' }
try {
    $summary.LastBootUpTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
    Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID,Status,ConfigManagerErrorCode |
        ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Output 'display-devices.json')
} catch { $_ | Out-String | Set-Content (Join-Path $Output 'device-query-error.txt') }
$smi = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
if ($smi) {
    try {
        & $smi.Source -q 1> (Join-Path $Output 'nvidia-smi.txt') 2> (Join-Path $Output 'nvidia-smi-error.txt')
        $summary.NvidiaSmiExitCode = $LASTEXITCODE
    } catch { $_ | Out-String | Set-Content (Join-Path $Output 'nvidia-smi-error.txt') }
}
if ($Benchmark) {
    $resolved = (Resolve-Path -LiteralPath $Benchmark).Path
    $summary.BenchmarkSha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
    $summary.BenchmarkState = 'failed'
    try {
        & $resolved 1> (Join-Path $Output 'benchmark.json') 2> (Join-Path $Output 'benchmark-error.txt')
        $summary.BenchmarkExitCode = $LASTEXITCODE
        if ($LASTEXITCODE -eq 0) {
            $data = Get-Content -Raw (Join-Path $Output 'benchmark.json') | ConvertFrom-Json
            if ([string]::IsNullOrWhiteSpace($data.gpu) -or $data.pci -ne '0000:01:00.0' -or @($data.benchmarks).Count -ne 3 -or
                @($data.benchmarks | Where-Object { $_.reference_checked -ne $true -or $_.gflops -le 0 }).Count) {
                throw 'Benchmark output did not pass identity/result checks.'
            }
            $summary.BenchmarkState = 'passed'
        }
    } catch { $_ | Out-String | Add-Content (Join-Path $Output 'benchmark-error.txt') }
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Output 'summary.json')
Write-Host "Saved results: $Output"
Write-Host "Benchmark: $($summary.BenchmarkState). Use the same executable and driver as the baseline for comparison."
Write-Host 'Do not reboot before collecting results. This script does not apply the patch or read GPU selector registers.'
