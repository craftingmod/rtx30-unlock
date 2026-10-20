@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
set "CC=%GU_CC%"
set "FLAGS=-std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -fno-builtin"
if "%~1"=="red" (
  "%CC%" %FLAGS% -DGU_EMPTY_RUNTIME tests\test_runtime.c -o build\runtime-red.exe
  if errorlevel 1 exit /b 1
  build\runtime-red.exe
  exit /b 1
)
"%CC%" %FLAGS% -Dmemcpy=GuTestMemcpy -Dmemset=GuTestMemset -Dmemchr=GuTestMemchr -Dmemcmp=GuTestMemcmp -Dstrcmp=GuTestStrcmp -Dstrlen=GuTestStrlen -c runtime.c -o build\runtime-host.obj
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% tests\test_runtime.c format.c build\runtime-host.obj -o build\runtime-tests.exe
if errorlevel 1 exit /b 1
build\runtime-tests.exe
exit /b %errorlevel%
