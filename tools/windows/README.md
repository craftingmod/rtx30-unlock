# tools/windows

PowerShell helpers for the alternative "no USB stick" installation described in
[`docs/windows-internal-install.md`](../../docs/windows-internal-install.md).

| script | purpose |
|---|---|
| `install-internal.ps1` | create a hidden ESP-type FAT32 partition on the internal GPT disk, copy the unlocker package there, add `RTX30 UNLOCK` as the first UEFI boot entry, hide the partition |
| `remove-internal.ps1` | delete the boot entry (and optionally the partition) |

Both scripts require an elevated PowerShell. They never touch the Windows ESP or the
original *Windows Boot Manager* entry, and every change they do is listed in
`remove-internal.ps1` / explained in the docs.

Example package layout (`-PackageDir`):

```
package\
  EFI\BOOT\BOOTX64.EFI          <- bun run build
  RTX-UNLOCK\
    ASSETS\                     <- bun run firmware:assets  (5 files, not in this repo)
    PROFILE.BIN                 <- captured on this machine
    DEBUG.ON                    <- optional; delete for fully automatic boots
```
