@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_port.c port.c -o build\test-port.exe
if errorlevel 1 exit /b 1
build\test-port.exe
if errorlevel 1 exit /b 1
call test-master-attributes.cmd
exit /b %errorlevel%
