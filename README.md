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
- LLVM with `clang` and `lld-link`; add its `bin` directory to `PATH` or set `LLVM_BIN` to that directory
- The EDK2 MdePkg headers under `deps/edk2/MdePkg/Include` (included in this repository)

Python 3.9+ is used by the existing test and packaging utilities, not by `bun run build`.

```sh
bun install
bun run build
```

The EFI image is written to `build/efi/BOOTX64.EFI`.

### Build EFI zip

```sh
bun run firmware:assets
bun run build:zip
```

It contains `EFI/BOOT/BOOTX64.EFI`, `RTX-UNLOCK/UNLOCK.md`, and, when
`build/firmware-assets` exists, its contents under `RTX-UNLOCK/ASSETS/`.

### Firmware extractor

```sh
bun run build:firmware-exe
```
Run the EXE from the USB drive root to download and generate firmware in
`RTX-UNLOCK/ASSETS`.
