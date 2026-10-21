@echo off
setlocal
cd /d "%~dp0..\.."
call experiments\gop\test-external-profile.cmd
if errorlevel 1 exit /b 1
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
set "INC=-I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64"
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror %INC% experiments/gop/test_auto_profile.c probe_profile.c probe_core.c unlock/sha256.c -o build/gop-experiment/auto-encode-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\auto-encode-tests.exe build\runtime-profile-GA102\PROFILE.BIN build\runtime-profile-GA103\PROFILE.BIN build\runtime-profile-GA104\PROFILE.BIN build\runtime-profile-GA106\PROFILE.BIN build\runtime-profile-GA107\PROFILE.BIN
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS %INC% experiments/gop/test_auto_capture.c probe_app.c probe_core.c probe_profile.c unlock/profile_probe.c unlock/runtime_profile.c unlock/sha256.c -o build/gop-experiment/auto-capture-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\auto-capture-tests.exe
if errorlevel 1 exit /b 1
python -c "import sys;sys.path.insert(0,'tools');from pathlib import Path;from probe_profile import generate;from runtime_profile import encode;assert encode(generate('build/auto-profile-report.txt','build/auto-profile-prom.bin'))==Path('build/auto-profile.bin').read_bytes();print('PASS real C capture/profile equals Python output')"
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_FULL_FLR -DGC_FULL_UNLOCK -DGC_WINDOWS_HANDOFF -DGC_PRODUCTION -DGU_SSD_EXPERIMENT -DGU_RUNTIME_PROFILE_BUILD -DGU_AUTO_PROFILE %INC% experiments/gop/test_auto_flow.c probe_core.c unlock/runtime_profile.c unlock/format.c unlock/sha256.c -o build/gop-experiment/auto-flow-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\auto-flow-tests.exe build\runtime-profile-GA102\PROFILE.BIN
exit /b %errorlevel%
