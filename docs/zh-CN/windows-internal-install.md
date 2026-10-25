# 内置安装：每次冷启动免 U 盘、免按键

上游用法是每次冷启动从独立 U 盘运行 EFI。还有一种全自动做法：

* 在内置 GPT 磁盘上建一个小的 **ESP 类型 FAT32 隐藏分区**；
* 把 EFI 程序、5 个固件资产、本机验证过的 `PROFILE.BIN` 复制进去；
* 新建一条指向 `\EFI\BOOT\BOOTX64.EFI` 的 UEFI 启动项，并**排到固件启动顺序第一**。

这样每次冷启动固件先跑解锁器 → 自动解锁 → 直接加载 Windows ESP 上的
`bootmgfw.efi`，无需重启、无需按键、无需 U 盘。原 Windows Boot Manager 条目保持不动。

## 前提

- UEFI 启动、**Secure Boot 关闭**（否则程序拒绝运行）；
- 管理员权限（一次性安装）；
- 自己构建的 `BOOTX64.EFI` + `bun run firmware:assets` 生成的 5 个资产
  （资产是 NVIDIA 固件/生成产物，**不在本仓库里**，见 `tools/firmware/README.md`）；
- 本机采集的 `RTX-UNLOCK\PROFILE.BIN`（见 `docs/agent/EXTERNAL-PROFILE.md`）。

## 快速开始

准备包目录：

```
package\
  EFI\BOOT\BOOTX64.EFI
  RTX-UNLOCK\
    ASSETS\   (5 个校验过的资产)
    PROFILE.BIN
    DEBUG.ON  (可选，仅首次运行用；删掉即全自动)
```

管理员 PowerShell：

```powershell
.\tools\windows\install-internal.ps1 -PackageDir C:\path\to\package
```

脚本会：导出 BCD 备份 → 从 C: 缩出 512MB 建 ESP 型 FAT32 分区 → 复制包 →
创建 `RTX30 UNLOCK` 启动项并排第一 → 移除盘符、设 GPT no-automount（隐藏分区）。

回退：

```powershell
.\tools\windows\remove-internal.ps1 -EntryGuid '{...}'
.\tools\windows\remove-internal.ps1 -EntryGuid '{...}' -DeletePartition
```

临时回退也可以开机按 **F8** 选 **Windows Boot Manager**（ASUS 主板）。

## 行为与注意

* 冷启动：固件 → 解锁器（自动）→ Windows，比平时多几秒；
* 断电后速率选择器复位，所以**每次冷启动都要重新解锁**（这是设计使然，不写显卡固件）；
* 睡眠/休眠可能复位；长任务建议关自动睡眠，需要时重启恢复；
* Windows 大版本更新 / BIOS 更新可能重置 UEFI 启动顺序，若解锁器不跑了，把
  `RTX30 UNLOCK` 重新排第一即可（`bcdedit /set {fwbootmgr} displayorder {GUID} /addfirst`）；
* **不要格式化/加密那个隐藏分区**（里面有资产和本机 profile）。

## 分区参数

| 项目 | 值 |
|---|---|
| 大小 | 默认 512MB（`-SizeMB`） |
| 文件系统 | FAT32 |
| GPT 类型 | `c12a7328-f81f-11d2-ba4b-00a0c93ec93b`（EFI System Partition） |
| 盘符 | 安装完成后移除 |
| GPT 属性 | `0x8000000000000000`（no automount） |
| 启动项 | `RTX30 UNLOCK`，设备=该分区，路径=`\EFI\BOOT\BOOTX64.EFI` |

程序只接受 USB 设备或 GPT ESP 类型分区作为引导卷（`GU_SSD_EXPERIMENT`），
所以分区要用 ESP 类型 GUID。
