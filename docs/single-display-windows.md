# Single-display support for the v20 "quiet profile" GOP experiment

## Problem

`full.c` `GfPrepare()` requires the target GPU to expose **exactly two vendor children**
in UEFI (one child with `GOP+EDID`, one sibling with `EDID` only) because that is the
topology the original author validated:

```c
s = Children(c, agents[0], &a, &b);
...
if (s == EFI_UNSUPPORTED) ... /* exactly two children required */
```

On a machine with a **single monitor** the NVIDIA UEFI driver usually creates only one
child, so `Children()` returns `EFI_UNSUPPORTED` and the run stops before touching any
GPU state:

```
FULL_CHILDREN_RESULT phase=prepare status=0x8000000000000003 a=<physical> b=0
CANCELLED_OR_REFUSED stage=full-prepare status=0x8000000000000003 touched=0
```

The workaround used in the field is to attach a second EDID source (second monitor,
dummy plug, or the second input of the same monitor). That is awkward on a
single-display machine.

## Patch

`src/gop/full.c` (27 insertions, 12 deletions):

1. `Children()` accepts one child; `*b` stays `NULL`.
2. `GfPrepare()` treats `sibling == NULL` as "single output": skips the sibling EDID
   driver (role 5), keeps the remaining role checks, and logs
   `FULL_TOPOLOGY children=1 sibling=0`.
3. `GfRun()` / `Detached()` conditionally skip the sibling stop/start steps and pass
   `1` instead of `2` children to the PCI driver `Stop()`.
4. The post-transaction restore path skips `Start(role 5)` when there is no sibling and
   guards the single-child case while re-detecting the children.

The detach → V67 unlock + FLR → GOP restore → Windows handoff sequence is unchanged.

## Tested

Stock **GA104-based RTX 3060 12 GB** (PCI `10de:2487`, subsystem `7377:2000`,
VBIOS `94.04.71.40.0F`), Windows 11 26300, NVIDIA 616.56, single DisplayPort monitor,
no iGPU, Secure Boot off:

```
FULL_TOPOLOGY children=1 sibling=0
FULL_READY: 1 vendor children; six roles verified; one FLR after full isolation
FULL_STOP_INTENT role=3..0 -> status=0
FULL_ISOLATION_RESULT status=0
PATCH_INTENT: one real unlock transaction including its own FLR; no extra reset
selector write: SS0=0x88888888
selector write: SS1=0x8
PCIe FLR returned: result=0
PATCH_TRANSACTION result=0 containment=0 started=1 quiesced=1 fault=0 retained=0 maps=0 FLR=1 safe=1
PATCH_STATE phase=before-gop SS0=88888888 SS1=00000008 PLM=ffffffff matched=1
PATCH_STATE phase=after-gop ... matched=1   (7 phases in total)
FULL_RESULT status=0 touched=1 detach=0 isolated=0 exclusive=0 close=0 restore=0 verify=0 restored=1
END: patch and display verified; Windows handoff follows if report close succeeds
WINDOWS_START_INTENT reboot=0
```

Measured with cuBLAS `FP16 in / FP32 accumulate` 4096³ GEMM on the same machine
(see `tools/bench/fp16_gemm_bench.py`):

| | before (locked) | after (unlocked) | gain |
|---|---:|---:|---:|
| FP16→FP32 accumulate GEMM | 24.00 TFLOPS | 37.96–44.33 TFLOPS | +58…+85 % |
| TF32 GEMM | 24.06 TFLOPS | 33.70–45.72 TFLOPS | +40…+90 % |

## Caveats

* Upstream only validated the two-output topology. The single-output path is a strict
  subset of the observed detach/restore sequence, but it is *our* test on *one* card;
  treat it as experimental.
* Safety behaviour is unchanged: every check before the manual `U` / automatic start
  refuses instead of writing to the GPU, and an uncertain cleanup halts with
  `STOP ... POWER OFF`.
* Display recovery after the FLR still goes through the GOP restore path; if the screen
  does not come back, power off physically and inspect `RTX-UNLOCK/RUNnn/GOP.TXT`.
