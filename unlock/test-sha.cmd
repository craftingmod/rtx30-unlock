@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
set "GU_SHA_SOURCE=sha256.c"
set "GU_SHA_TEST=build\sha-tests.exe"
if /I "%~1"=="--red" (
    set "GU_SHA_SOURCE=build\sha-red.c"
    set "GU_SHA_TEST=build\sha-red.exe"
) else if not "%~1"=="" (
    echo usage: test-sha.cmd [--red]
    exit /b 2
)
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_sha256.c "%GU_SHA_SOURCE%" -o "%GU_SHA_TEST%"
if errorlevel 1 exit /b 1
"%GU_SHA_TEST%" "..\analysis\pinned-assets\gsp_ga10x.bin" "..\evidence\boot-03\PROM.BIN"
if errorlevel 1 exit /b 1
if /I "%~1"=="--red" exit /b 0
"%GU_CC%" -target x86_64-pc-windows-msvc -std=c11 -Os -ffreestanding -fno-builtin -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra -Werror -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 -c sha256.c -o build\sha-freestanding.obj
if errorlevel 1 exit /b 1
echo PASS freestanding x86_64-pc-windows-msvc compilation
exit /b 0
