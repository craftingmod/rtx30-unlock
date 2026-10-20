@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -DGU_GOP_EXPERIMENT -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU -I unlock\compat -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 experiments\gop\test_patch_state.c unlock\app.c unlock\ownership.c unlock\checks.c unlock\sha256.c unlock\port.c unlock\compat.c unlock\vendor\unlock_610.c unlock\vendor\prep_610.c unlock\vendor\layout_610.c unlock\vendor\vbios_610.c unlock\vendor\gfw_eval.c -o build\gop-experiment\patch-state-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\patch-state-tests.exe
exit /b %errorlevel%
