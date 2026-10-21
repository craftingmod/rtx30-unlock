@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_windows.c -o build\gop-experiment\windows-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\windows-tests.exe
exit /b %errorlevel%
