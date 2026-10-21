@echo off
setlocal
cd /d "%~dp0..\.."
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_PRODUCTION -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_io.c src\gop\cycle.c src\unlock\format.c -o build\gop-experiment\production-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\production-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGU_GOP_EXPERIMENT -I unlock\compat -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_quiet_io.c src\unlock\format.c src\unlock\sha256.c -o build\gop-experiment\quiet-io-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\quiet-io-tests.exe
exit /b %errorlevel%
