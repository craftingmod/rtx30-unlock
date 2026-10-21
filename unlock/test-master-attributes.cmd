@echo off
setlocal
cd /d "%~dp0"
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU -I ..\src\unlock\compat -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 ..\src\unlock\tests\test_master_attributes.c ..\src\unlock\port.c ..\src\unlock\compat.c ..\src\unlock\vendor\unlock_610.c ..\src\unlock\vendor\prep_610.c ..\src\unlock\vendor\layout_610.c ..\src\unlock\vendor\vbios_610.c ..\src\unlock\vendor\gfw_eval.c -o build\master-attributes.exe
if errorlevel 1 exit /b 1
build\master-attributes.exe
exit /b %errorlevel%
