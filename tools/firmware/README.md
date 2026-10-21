# Pinned 610.43.03 firmware

These Bun scripts fetch and validate the pinned NVIDIA inputs, then reproduce
the five files loaded by the kernel module from `gpu-unlock/610.43.03/`.
Downloads use HTTPS, are checked against their SHA-256, and are cached
atomically. Cached files are rechecked on every run. Nothing downloaded is
executed or installed.

The package's `.tar.zst` is read with Bun's built-in zstd decompressor and tar
archive reader. The source `.tar.xz` is decoded with `xz-compat` and read with
Bun's tar archive reader. The scripts do not invoke system `tar`, `zstd`, or
`xz` commands.

Run from the repository root with Bun:

```sh
bun tools/firmware/fetch_artifacts.ts scratchpad/artifacts
bun tools/firmware/fetch_artifacts.ts scratchpad/artifacts --offline
```

The first command downloads the NVIDIA open kernel module source archive and
the exact CachyOS `nvidia-utils` package pinned in
`artifacts-610.43.03.json`. The offline command verifies that both cached
files are present and unchanged.

To fetch both inputs, validate them, and generate the five EFI assets in one
step, run `bun run firmware:assets`. Inputs are cached in
`build/firmware-inputs`; the output is `build/firmware-assets`. EFI packaging
can consume these assets in a separate step.

Generate the firmware files directly from the pinned archives:

```powershell
bun tools/firmware/generate_v67.ts scratchpad/artifacts/nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst build/firmware
bun tools/firmware/extract_boot_assets.ts scratchpad/artifacts/NVIDIA-kernel-module-source-610.43.03.tar.xz build/firmware
```

`generate_v67.ts` extracts `gsp_ga10x.bin` and the NVIDIA license from the
package and generates `v67-ga102-610.43.03.bin`. `extract_boot_assets.ts` can
also read an already-extracted source directory. It builds
`booter_load_ga102.bin`, `gsp_boot_ga102.bin`, and
`gsp_boot_desc_ga102.bin` from the source archive. Each script checks the
relevant inputs and outputs against the pinned SHA-256 values before writing.

See [NOTICE.md](NOTICE.md) for provenance and licensing. Python scripts in
`../firmware-python/` are retained as the original reference implementation.
