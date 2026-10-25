# Building on Windows without Visual Studio / Windows SDK

`bun run build` only needs **Bun**, **clang** and **lld-link**. If the machine has no
MSVC toolchain, this recipe (used for the build documented in
`docs/single-display-windows.md`) works with a portable LLVM/Clang distribution.

## 1. Bun

Portable, no installer needed: download `bun-windows-x64.zip` from the Bun releases and
add its `bin` directory to `PATH`, or use the official installer.

## 2. clang + lld-link

Any recent LLVM works. A convenient admin-free source is the LLVM bundled with the
**Android NDK** (`<ndk>/toolchains/llvm/prebuilt/windows-x86_64`):

* `clang.exe` is there, but there is no `lld-link.exe`; lld dispatches on `argv[0]`, so
  a copy of `ld.lld.exe` named `lld-link.exe` behaves exactly like `lld-link`.
* make a small shim directory:

```
toolchain\llvm-shim\
  clang.exe      (copy of <ndk>\bin\clang.exe)
  lld-link.exe   (copy of <ndk>\bin\ld.lld.exe)
```

* `clang.exe` needs the NDK DLLs (`libwinpthread.dll`, …): keep
  `<ndk>\bin` on `PATH`.
* `build.ts` looks up `clang`/`lld-link` through `LLVM_BIN`; point it at the shim:
  `set LLVM_BIN=...\toolchain\llvm-shim`.
* clang finds its built-in headers relative to the executable; either copy
  `<ndk>\lib\clang\<ver>\include` into `...\toolchain\llvm-shim\lib\clang\<ver>\include`
  or add a directory junction.

## 3. Missing libc headers (no Windows SDK)

The EFI sources include `<string.h>`, `<errno.h>`, `<stdint.h>`, `<stddef.h>`,
`<stdbool.h>`, `<stdarg.h>`. The last four come from clang's resource directory; the
first two are provided here as declaration-only shims (the implementations live in the
repository: `src/unlock/runtime.c` / `compat.c`):

```
set CPATH=<repo>\tools\win32\headers
```

(`tools/win32/headers/string.h` and `errno.h` are in this repository.)

## 4. Build

```powershell
$env:PATH      = "<bun>\bin;<ndk>\bin;" + $env:PATH
$env:LLVM_BIN  = "<repo>\toolchain\llvm-shim"
$env:CPATH     = "<repo>\tools\win32\headers"
cd <repo>
bun install
bun run build
# -> build\efi\BOOTX64.EFI
bun run firmware:assets      # -> build\firmware-assets\ (5 assets, needs network)
```

## Reproducibility note

The official release `BOOTX64.EFI` is 107 520 bytes; a rebuild from the same source
and flags is also 107 520 bytes but **not byte-identical** (different clang/LLVM
versions produce a different `.text` layout; `.data` matches). Treat the build as
reproducible in behaviour, not byte-for-byte.

## Tests

`bun run test` builds host mock tests for Windows; they need a host C compiler that can
link and run Win32 executables (`CC`). With the NDK clang this fails (no mingw CRT);
a portable MSYS2 mingw-w64 clang 23 works:

```powershell
$env:CC = "<msys2>\mingw64\bin\clang.exe"
bun run test
```
