# Firmware provenance and distribution scope

`gsp_ga10x.bin` is extracted byte-for-byte from the checksummed NVIDIA
610.43.03 package in `artifacts-610.43.03.json`. The Booter and GSP boot images
are decoded unchanged from the pinned 610.43.03 source archive. The extraction
also preserves that package's complete `usr/share/licenses/nvidia-utils/LICENSE`
as `NVIDIA-LICENSE`. It is installed with the firmware package.

The source archive's open-module licensing does **not** license NVIDIA firmware.
The binary package contains the NVIDIA Driver License Agreement. Its section
1.1(d) addresses distribution for open source operating-system kernels with
unchanged binary files and inclusion of the agreement. Additional limitations
apply, including NVIDIA-platform use. Personal local packaging is the initial
delivery target. Review the full pinned agreement and the separately generated
payload before any public distribution; redistribution approval is not claimed.

The V67 constants are adapted from WildFlash1st/cmp90hx-unlock at commit
`76940ae583281daa2b883518a2e89b56c11d3a46`, patch
`driver/patches/cmp90-rejoin15/0014-6104303-cmp90hx-stockflow-rejoin14-multigpu-state.patch`,
function `_kgspCmp90FillV67Signature`, reproduced by
`tools/firmware/generate_v67.ts`. The repository's actual `LICENSE` is
GPL version 2; the earlier manifest's MIT label was incorrect. That license is
preserved in `licenses/GPL-2.0.txt`. The generated image is a separate file;
the extracted NVIDIA images are never rewritten.

NVIDIA source files inspected during the loader audit carry their own MIT
copyright notices. The minimum helper's metadata layout and Falcon operations
follow those source contracts; `licenses/NVIDIA-MIT.txt` preserves the notice.
The inspected EFI reference's notice is in `licenses/CMP40HX-MIT.txt`.
The reference changes a CMP 90HX, not an RTX 3090, and is not evidence of
standalone boot or persistence through NVIDIA 615.71.09.
