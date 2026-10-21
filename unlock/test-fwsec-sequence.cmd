@echo off
setlocal
cd /d "%~dp0"
python ..\src\unlock\tests\check_fwsec_sequence.py --prepare
if errorlevel 1 exit /b 1
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
set "TRACE_CLANG=%GU_CC%"
set "TRACE_FLAGS=-std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU -I ..\src\unlock\vendor -I ..\src\unlock\compat -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64"
set "TRACE_SOURCES=..\src\unlock\tests\test_fwsec_trace.c ..\src\unlock\port.c ..\src\unlock\compat.c ..\src\unlock\vendor\prep_610.c ..\src\unlock\vendor\layout_610.c ..\src\unlock\vendor\vbios_610.c ..\src\unlock\vendor\gfw_eval.c"
"%TRACE_CLANG%" %TRACE_FLAGS% %TRACE_SOURCES% ..\src\unlock\vendor\unlock_610.c -o build\fwsec-current-trace.exe
if errorlevel 1 exit /b 1
"%TRACE_CLANG%" %TRACE_FLAGS% %TRACE_SOURCES% build\reference-v06\vendor\unlock_610.c -o build\fwsec-v06-trace.exe
if errorlevel 1 exit /b 1
build\fwsec-current-trace.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets" build\fwsec-current.trace
if errorlevel 1 exit /b 1
build\fwsec-v06-trace.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets" build\fwsec-v06.trace
if errorlevel 1 exit /b 1
python ..\src\unlock\tests\check_fwsec_sequence.py
exit /b %errorlevel%
