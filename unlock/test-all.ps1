param([switch]$WithLocalFixtures)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$logDir=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$jobs=@(
 @{Name='probe-build';Kind='powershell';Path='scripts/build-probe.ps1'},
 @{Name='gop-experiment-build';Kind='powershell';Path='scripts/build-gop-experiment.ps1'},
 @{Name='gop-experiment-cycle';Kind='cmd';Path='experiments/gop/test.cmd'},
 @{Name='gop-experiment-usb';Kind='python';Path='tools/test_gop_experiment.py'},
 @{Name='unlock-build';Kind='powershell';Path='unlock/build.ps1'},
 @{Name='probe';Kind='cmd';Path='test.cmd'},
 @{Name='probe-profile';Kind='python';Path='tools/test_probe_profile.py'},
 @{Name='probe-usb';Kind='python';Path='tools/test_probe_usb.py'},
 @{Name='profile-runtime';Kind='cmd';Path='unlock/test-profile.cmd'},
 @{Name='external-profile-format';Kind='python';Path='tools/test_runtime_profile.py'},
 @{Name='auto-profile-runtime';Kind='cmd';Path='experiments/gop/test-auto-profile.cmd'},
 @{Name='gop-production';Kind='cmd';Path='experiments/gop/test-production.cmd'},
 @{Name='gop-ssd';Kind='cmd';Path='experiments/gop/test-ssd.cmd'},
 @{Name='port';Kind='cmd';Path='unlock/test-port.cmd'},
 @{Name='checks';Kind='cmd';Path='unlock/test-checks.cmd'},
 @{Name='runtime';Kind='cmd';Path='unlock/test-runtime.cmd'},
 @{Name='pe';Kind='python';Path='unlock/tests/test_pe.py'},
 @{Name='usb-package';Kind='python';Path='tools/test_package_usb.py'}
)
if($WithLocalFixtures) {
 $jobs+=@(
  @{Name='transaction';Kind='cmd';Path='unlock/test-transaction.cmd'},
  @{Name='sha';Kind='cmd';Path='unlock/test-sha.cmd'},
  @{Name='efi-transaction';Kind='cmd';Path='unlock/test-efi-transaction.cmd'},
  @{Name='app';Kind='cmd';Path='unlock/test-app.cmd'},
  @{Name='fwsec-sequence';Kind='cmd';Path='unlock/test-fwsec-sequence.cmd'}
 )
}
$results=@()
foreach($job in $jobs) {
 $log=Join-Path $logDir ($job.Name+'.log')
 $code=1
 try {
  $ErrorActionPreference='Continue'
  $path=Join-Path $project $job.Path
  if($job.Kind -eq 'cmd') { & $path *> $log }
  elseif($job.Kind -eq 'powershell') {
   Push-Location
   try { & $path *> $log } finally { Pop-Location }
  }
  else { & python $path *> $log }
  $code=$LASTEXITCODE
 } catch { $_ | Out-String | Add-Content -LiteralPath $log }
 finally { $ErrorActionPreference='Stop' }
 $results += [ordered]@{Name=$job.Name;Command=$job.Path;ExitCode=$code;Log=($job.Name+'.log')}
 Write-Output ($job.Name+': exit '+$code)
 Get-Content -Tail 1 -LiteralPath $log
}
$passed=@($results | Where-Object {$_.ExitCode -ne 0}).Count -eq 0
[ordered]@{AllPassed=$passed;HardwareAccess=$false;WithLocalFixtures=[bool]$WithLocalFixtures;Results=$results} |
 ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $logDir 'test-results.json')
if(-not $passed){throw 'One or more verification groups failed.'}
Write-Output ('All '+$jobs.Count+' selected verification groups passed; hardware was simulated.')
