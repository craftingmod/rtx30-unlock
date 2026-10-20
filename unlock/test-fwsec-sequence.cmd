@echo off
setlocal
cd /d "%~dp0"
python tests\check_fwsec_sequence.py --prepare
if errorlevel 1 exit /b 1
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
set "TRACE_CLANG=%GU_CC%"
set "TRACE_FLAGS=-std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU -I vendor -I compat -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64"
set "TRACE_SOURCES=tests\test_fwsec_trace.c port.c compat.c vendor\prep_610.c vendor\layout_610.c vendor\vbios_610.c vendor\gfw_eval.c"
"%TRACE_CLANG%" %TRACE_FLAGS% %TRACE_SOURCES% vendor\unlock_610.c -o build\fwsec-current-trace.exe
if errorlevel 1 exit /b 1
"%TRACE_CLANG%" %TRACE_FLAGS% %TRACE_SOURCES% build\reference-v06\vendor\unlock_610.c -o build\fwsec-v06-trace.exe
if errorlevel 1 exit /b 1
build\fwsec-current-trace.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets" build\fwsec-current.trace
if errorlevel 1 exit /b 1
build\fwsec-v06-trace.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets" build\fwsec-v06.trace
if errorlevel 1 exit /b 1
python tests\check_fwsec_sequence.py
exit /b %errorlevel%
