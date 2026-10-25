# 中文说明

上游仓库的文档主要是韩文实验记录。这个分支补充了两份中文文档：

- [单显示器支持（补丁说明与实测证据）](single-display-windows.md)
  —— 为什么 `full.c` 原本要求"恰好 2 个显示子设备"，改了什么，实测日志与
  cuBLAS GEMM 性能对比（解锁前 24.00 TFLOPS → 解锁后 37.96~44.33 TFLOPS）。
- [内置安装（免 U 盘、免按键）](windows-internal-install.md)
  —— 把解锁器装到内置 NVMe 的隐藏 ESP 型 FAT32 分区，并排到 UEFI 启动第一，
  以后开机自动解锁进 Windows；含回退方法。

对应的英文文档在 `docs/` 下：

- `docs/single-display-windows.md`
- `docs/windows-internal-install.md`
- `docs/build-without-msvc.md`（没有 Visual Studio/Windows SDK 时如何构建）
- `tools/windows/`（安装/卸载脚本）
- `tools/bench/fp16_gemm_bench.py`（验证解锁用的 cuBLAS FP16/FP32 累加基准）
