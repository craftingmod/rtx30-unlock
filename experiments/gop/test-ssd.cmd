@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -DGU_SSD_EXPERIMENT -DGU_GOP_EXPERIMENT -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU -I unlock\compat -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 experiments\gop\test_patch_state.c unlock\app.c unlock\ownership.c unlock\checks.c unlock\sha256.c unlock\port.c unlock\compat.c unlock\vendor\unlock_610.c unlock\vendor\prep_610.c unlock\vendor\layout_610.c unlock\vendor\vbios_610.c unlock\vendor\gfw_eval.c -o build\gop-experiment\ssd-selector-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\ssd-selector-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGU_GOP_EXPERIMENT -DGU_SSD_EXPERIMENT -I unlock\compat -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 experiments\gop\test_quiet_io.c unlock\format.c unlock\sha256.c -o build\gop-experiment\ssd-source-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\ssd-source-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_FULL_FLR -DGC_FULL_UNLOCK -DGC_WINDOWS_HANDOFF -DGC_PRODUCTION -DGU_SSD_EXPERIMENT -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 experiments\gop\test_ssd_flow.c unlock\format.c -o build\gop-experiment\ssd-flow-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\ssd-flow-tests.exe
exit /b %errorlevel%
