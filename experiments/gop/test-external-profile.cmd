@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
for %%F in (GA102 GA103 GA104 GA106 GA107) do (
  python tools\test_probe_profile.py --write-fixture build\runtime-profile-%%F %%F
  if errorlevel 1 exit /b 1
  python -c "import sys;sys.path.insert(0,'tools');from runtime_profile import encode;from check_profile import check;from pathlib import Path;p=Path('build/runtime-profile-%%F');(p/'PROFILE.BIN').write_bytes(encode(check(p/'board.json',experimental=True)))"
  if errorlevel 1 exit /b 1
)
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGU_RUNTIME_PROFILE_BUILD -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\unlock\tests\test_runtime_profile.c src\unlock\runtime_profile.c src\unlock\sha256.c src\unlock\checks.c src\unlock\port.c src\unlock\vendor\layout_610.c -o build\gop-experiment\external-profile-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\external-profile-tests.exe build\runtime-profile-GA102\PROFILE.BIN build\runtime-profile-GA103\PROFILE.BIN build\runtime-profile-GA104\PROFILE.BIN build\runtime-profile-GA106\PROFILE.BIN build\runtime-profile-GA107\PROFILE.BIN
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_FULL_FLR -DGC_FULL_UNLOCK -DGC_WINDOWS_HANDOFF -DGC_PRODUCTION -DGU_SSD_EXPERIMENT -DGU_RUNTIME_PROFILE_BUILD -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_external_flow.c src\unlock\format.c src\unlock\runtime_profile.c src\unlock\sha256.c -o build\gop-experiment\external-flow-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\external-flow-tests.exe build\runtime-profile-GA102\PROFILE.BIN
exit /b %errorlevel%
