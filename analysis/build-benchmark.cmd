@echo off
setlocal
cd /d "%~dp0"
set "GU_VS="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GU_VS=%%i"
if not defined GU_VS (
  echo Visual Studio C++ Build Tools not found. 1>&2
  exit /b 1
)
call "%GU_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "GU_NVCC=nvcc.exe"
if exist "%CUDA_PATH%\bin\nvcc.exe" set "GU_NVCC=%CUDA_PATH%\bin\nvcc.exe"
set "SOURCE=compute-benchmark.cu"
if not exist "%SOURCE%" set "SOURCE=..\src\gop\compute-benchmark.cu"
"%GU_NVCC%" -std=c++17 -O3 -arch=sm_86 -Xcompiler /utf-8 "%SOURCE%" -lcublas -o compute-benchmark.exe
exit /b %errorlevel%
