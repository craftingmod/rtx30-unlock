# 单显示器支持（补丁说明与实测证据）

## 问题

`full.c` 的 `GfPrepare()` 原样要求目标显卡在 UEFI 下枚举出**恰好 2 个厂商子设备**
（一个 `GOP+EDID`，一个仅 `EDID`），因为作者只在那种双输出拓扑上验证过。

单显示器机器上 NVIDIA UEFI 驱动通常只创建 1 个子设备，于是 `Children()` 返回
`EFI_UNSUPPORTED`，在**写任何 GPU 状态之前**停下：

```
FULL_CHILDREN_RESULT phase=prepare status=0x8000000000000003 a=<physical> b=0
CANCELLED_OR_REFUSED stage=full-prepare status=0x8000000000000003 touched=0
```

现场常用绕法是把同一台显示器的 DP + HDMI 两个输入口都接到显卡（或插 EDID 假装载），
让固件看到 2 个输出；单显示器用户很不方便。

## 补丁内容（`src/gop/full.c`，27 增 / 12 删）

1. `Children()` 接受 1 个子设备（`*b` 保持 `NULL`）；
2. `GfPrepare()` 在无 sibling 时跳过副输出 EDID 驱动（role 5）的校验，日志打印
   `FULL_TOPOLOGY children=1 sibling=0`；
3. `GfRun()` / `Detached()` 条件化跳过 sibling 的 stop/start，PCI 驱动 `Stop()`
   的子设备数按 1 传入；
4. 解锁后的恢复路径同样在无 sibling 时跳过 `Start(role 5)`，并对单子设备重新探测做判空。

`detach → V67 解锁 + FLR → GOP 恢复 → Windows 交接` 主流程不变。

## 实测

环境：**GA104 版 RTX 3060 12GB**（PCI `10de:2487`，subsystem `7377:2000`，
VBIOS `94.04.71.40.0F`）、Windows 11 26300、NVIDIA 616.56、单 DisplayPort 显示器、
无核显、Secure Boot 关闭。

```
FULL_TOPOLOGY children=1 sibling=0
FULL_READY: 1 vendor children; six roles verified; one FLR after full isolation
FULL_STOP_INTENT role=3..0 -> status=0
FULL_ISOLATION_RESULT status=0
selector write: SS0=0x88888888
selector write: SS1=0x8
PCIe FLR returned: result=0
PATCH_TRANSACTION result=0 FLR=1 safe=1
PATCH_STATE ×7 matched=1（SS0=88888888 SS1=8 PLM=ffffffff）
FULL_RESULT status=0 touched=1 restored=1
END: patch and display verified; WINDOWS_START_INTENT reboot=0
```

cuBLAS `FP16 输入 / FP32 累加` 4096³ GEMM（`tools/bench/fp16_gemm_bench.py`）：

| 指标 | 解锁前 | 解锁后 | 提升 |
|---|---:|---:|---:|
| FP16→FP32 累加 GEMM | 24.00 TFLOPS | 37.96–44.33 TFLOPS | +58…+85 % |
| TF32 GEMM | 24.06 TFLOPS | 33.70–45.72 TFLOPS | +40…+90 % |

## 注意

- 上游只验证过双输出拓扑；单输出路径是我们在一张卡上的实测，请视为实验性功能。
- 安全行为不变：按 `U`/自动开始之前的所有检查都是"拒绝而不是写硬件"；清理不确定时
  仍会停在 `STOP ... POWER OFF`。
- 解锁后画面靠 GOP 恢复；若黑屏，物理断电并查看 `RTX-UNLOCK/RUNnn/GOP.TXT`。
