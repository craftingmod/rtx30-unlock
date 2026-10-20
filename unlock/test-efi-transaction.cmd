@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
set "EFI_TX_TARGET=0x220610deU"
set "EFI_TX_NAME=efi-transaction-3080"
if "%~1"=="red-target" set "EFI_TX_TARGET=0x220410deU"
if "%~1"=="red-target" set "EFI_TX_NAME=efi-transaction-red-target"
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=%EFI_TX_TARGET% -I compat -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_efi_transaction.c port.c compat.c vendor\unlock_610.c vendor\prep_610.c vendor\layout_610.c vendor\vbios_610.c vendor\gfw_eval.c -o build\%EFI_TX_NAME%.exe
if errorlevel 1 exit /b 1
build\%EFI_TX_NAME%.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets"
if errorlevel 1 exit /b 1
if "%~1"=="red-target" exit /b 0
call test-fwsec-sequence.cmd
exit /b %errorlevel%
