@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_visual.c src\unlock\format.c -o build\gop-experiment\visual-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\visual-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGU_GOP_EXPERIMENT -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_timed_flr.c src\unlock\port.c -o build\gop-experiment\timed-flr-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\timed-flr-tests.exe
exit /b %errorlevel%
