@echo off
setlocal
cd /d "%~dp0..\.."
if not exist build\gop-experiment mkdir build\gop-experiment
call scripts\host-env.cmd
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_cycle.c src\gop\cycle.c -o build\gop-experiment\cycle-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\cycle-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_io.c src\gop\cycle.c src\unlock\format.c -o build\gop-experiment\io-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\io-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_tree.c src\gop\tree.c -o build\gop-experiment\tree-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\tree-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_TREE_ONLY -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_io.c src\gop\tree.c src\unlock\format.c -o build\gop-experiment\tree-app-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\tree-app-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_leaf.c src\gop\leaf.c -o build\gop-experiment\leaf-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\leaf-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_LEAF_ONLY -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_io.c src\gop\leaf.c src\unlock\format.c -o build\gop-experiment\leaf-app-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\leaf-app-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_full.c src\gop\full.c src\gop\reset.c src\gop\leaf.c -o build\gop-experiment\full-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\full-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_FULL_FLR -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_io.c src\gop\full.c src\gop\reset.c src\gop\leaf.c src\unlock\format.c -o build\gop-experiment\full-app-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\full-app-tests.exe
if errorlevel 1 exit /b 1
"%GU_CC%" -std=c11 -O2 -Wall -Wextra -Werror -DGC_FULL_UNLOCK -I deps\edk2\MdePkg\Include -I deps\edk2\MdePkg\Include\X64 src\gop\test_patch.c src\gop\full.c src\gop\reset.c src\gop\leaf.c -o build\gop-experiment\patch-tests.exe
if errorlevel 1 exit /b 1
build\gop-experiment\patch-tests.exe
if errorlevel 1 exit /b 1
exit /b %errorlevel%
