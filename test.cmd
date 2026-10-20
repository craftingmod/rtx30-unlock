@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0scripts\host-env.cmd"
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64 test_core.c probe_core.c probe_app.c unlock/sha256.c -o build/test_core.exe
if errorlevel 1 exit /b 1
build\test_core.exe
exit /b %errorlevel%
