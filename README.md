# RTX30 Unlock

RTX 30 Series SS0 unlocker for windows by UEFI.

Only tested on specific setup, other environment isn't tested so USE IT CAUTION!

## How to use

1. Download compiled EFI zip from Github Releases.
2. Extract to some folder and download `firmware-assets.exe` to same folder.
3. Run `firmware-assets.exe` to fetch.
4. Remove `build`, `firmware-assets.exe` on root.
5. move folders to your seperate EFI partition (SSD or USB). DO NOT PLACE ON WINDOWS EFI!
6. Boot with seperate EFI and run windows.

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

## LICNESE

This project is based on [rtx3080-unlock-public](https://github.com/rough2515-hub/rtx3080-unlock-public) with [gpu-unlock](https://github.com/Zagitta/gpu-unlock).

Other LICENSE are on [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md) and LICENSES folder.