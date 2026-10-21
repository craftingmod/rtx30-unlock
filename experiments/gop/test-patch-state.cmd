@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -DGU_GOP_EXPERIMENT -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU -I unlock\compat -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_patch_state.c src\unlock\app.c src\unlock\ownership.c src\unlock\checks.c src\unlock\sha256.c src\unlock\port.c src\unlock\compat.c src\unlock\vendor\unlock_610.c src\unlock\vendor\prep_610.c src\unlock\vendor\layout_610.c src\unlock\vendor\vbios_610.c src\unlock\vendor\gfw_eval.c -o build\gop-experiment\patch-state-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\patch-state-tests.exe
exit /b %errorlevel%
