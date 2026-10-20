@echo off
if not defined GU_CC (
  if defined LLVM_BIN (
    set "GU_CC=%LLVM_BIN%\clang.exe"
  ) else (
    set "GU_CC=clang.exe"
  )
)
where "%GU_CC%" >nul 2>nul
if errorlevel 1 set "GU_CC=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
