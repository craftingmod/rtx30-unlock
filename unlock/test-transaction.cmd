@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "%~dp0..\scripts\host-env.cmd"
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_TARGET_PCI_ID=0x220610deu -I tests/transaction_shim tests/test_transaction_3080.c vendor/prep_610.c vendor/layout_610.c vendor/vbios_610.c vendor/gfw_eval.c -o build/transaction-3080.exe
if errorlevel 1 exit /b 1
build\transaction-3080.exe "..\evidence\boot-03\PROM.BIN" "..\analysis\pinned-assets"
exit /b %errorlevel%
