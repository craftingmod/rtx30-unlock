# RTX30 Unlock

RTX 30 Series SS0 unlocker for windows by UEFI.

## How to use

TODO edit

## Checked environment
* Colorful RTX 3090 ti: PCI `10DE:2203`, subsystem `7377:2000`, VBIOS `94.02.A0.00.08`
* Windows 11 26H2 (26300.9807)
* NVIDIA driver 615.56

## Build

### Requirements

- Bun
- Python 3.9+ available as `python`
- LLVM with `clang` and `lld-link`; add its `bin` directory to `PATH` or set `LLVM_BIN` to that directory
- The EDK2 MdePkg headers under `deps/edk2/MdePkg/Include` (included in this repository)

PowerShell is only needed for the `.ps1` build workflows; `bun run build` does not require it.

```sh
bun install
bun run build
```

The EFI image is written to `build/efi/BOOTX64.EFI`.
