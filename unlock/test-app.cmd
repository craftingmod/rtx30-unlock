@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
set "APP_SRC=..\src\unlock\app.c ..\src\unlock\app_io.c ..\src\unlock\ownership.c ..\src\unlock\format.c ..\src\unlock\sha256.c ..\src\unlock\checks.c"
set "APP_DEF="
if "%~1"=="red" set "APP_SRC="
if "%~1"=="red" set "APP_DEF=-DGU_EMPTY_APP"
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220610deU %APP_DEF% -I ..\src\unlock\compat -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 ..\src\unlock\tests\test_app.c %APP_SRC% ..\src\unlock\port.c ..\src\unlock\compat.c ..\src\unlock\vendor\unlock_610.c ..\src\unlock\vendor\prep_610.c ..\src\unlock\vendor\layout_610.c ..\src\unlock\vendor\vbios_610.c ..\src\unlock\vendor\gfw_eval.c -o build\app-tests.exe
if errorlevel 1 exit /b 1
build\app-tests.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets" "..\evidence\boot-03\REPORT.TXT" %~2
exit /b %errorlevel%
