@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
for %%F in (GA102 GA103 GA104 GA106 GA107) do (
  python ..\tools\test_probe_profile.py --write-fixture build\profile-fixture-%%F %%F
  if errorlevel 1 exit /b 1
  "%GU_CC%" -std=c11 -Wall -Wextra -Werror -include build/profile-fixture-%%F/profile.h -I ../deps/edk2/MdePkg/Include -I ../deps/edk2/MdePkg/Include/X64 ../src/unlock/tests/test_profile.c ..\src\unlock\checks.c ..\src\unlock\port.c ../src/unlock/vendor/layout_610.c -o build/test-profile-%%F.exe
  if errorlevel 1 exit /b 1
  build\test-profile-%%F.exe
  if errorlevel 1 exit /b 1
)
exit /b 0
