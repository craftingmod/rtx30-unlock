# Installing the unlocker on the internal disk (no USB stick)

The upstream workflow runs the EFI application from a separate USB stick on every cold
boot. This document describes an alternative that is fully automatic:

* a small hidden FAT32 partition of type **EFI System Partition** is created on the
  internal GPT disk,
* the EFI application, the five firmware assets and a validated `PROFILE.BIN` are
  copied there,
* a new UEFI boot entry pointing at `\EFI\BOOT\BOOTX64.EFI` is created and put **first**
  in the firmware boot order.

On every cold boot the firmware runs the unlocker first, it unlocks the card and then
loads `\EFI\Microsoft\Boot\bootmgfw.efi` from the Windows ESP – no reboot, no key press,
no USB stick. The original *Windows Boot Manager* entry is left untouched.

## Requirements

* UEFI boot, **Secure Boot disabled** (the application refuses to run otherwise).
* Administrator rights for the one-time installation.
* The built `BOOTX64.EFI` plus `RTX-UNLOCK\ASSETS\*` from
  `bun run firmware:assets` (the firmware assets are **not** in this repository:
  they are NVIDIA firmware / generated payloads, see `tools/firmware/README.md`).
* A `RTX-UNLOCK\PROFILE.BIN` captured on **this** machine (see
  `docs/agent/EXTERNAL-PROFILE.md`). Without it the first run captures one — with
  `DEBUG.ON` present it still asks for `U`; without `DEBUG.ON` it proceeds silently.

## Quick start

Prepare a package folder:

```
package\
  EFI\BOOT\BOOTX64.EFI
  RTX-UNLOCK\
    ASSETS\   (five verified assets)
    PROFILE.BIN
    DEBUG.ON  (optional, first run only)
```

Then run:

```powershell
# elevated PowerShell
.\tools\windows\install-internal.ps1 -PackageDir C:\path\to\package
```

The script

1. exports the current BCD to `<PackageDir>\bcd-backup.bcd`,
2. shrinks `C:` by `-SizeMB` (default 512) and creates an ESP-type FAT32 partition,
3. copies the package, creates the `RTX30 UNLOCK` boot entry and puts it first,
4. removes the drive letter and sets the GPT no-automount attribute so the partition
   is hidden like the Windows ESP.

Rollback:

```powershell
# elevated
.\tools\windows\remove-internal.ps1 -EntryGuid '{...}'      # delete the boot entry
.\tools\windows\remove-internal.ps1 -EntryGuid '{...}' -DeletePartition
```

or simply pick **Windows Boot Manager** in the firmware boot menu (F8 on ASUS boards)
for a one-off normal boot.

## Boot flow / behaviour

* Cold boot: firmware → unlocker (auto) → Windows. Takes a few extra seconds.
* `DEBUG.ON` present → the tool requires `U` once, writes `RUNnn` logs and captures
  probe data; removing it makes the boot fully silent/automatic.
* Power loss resets the unlocked rate selectors, so **every cold boot** has to unlock
  again — that is by design, nothing is persisted on the card.
* Suspend/hibernate may reset the selectors; reboot to restore the unlocked state.
* Windows feature updates or BIOS updates can reorder UEFI boot entries. If the
  unlocker stops running, put the `RTX30 UNLOCK` entry back first
  (`bcdedit /set {fwbootmgr} displayorder {GUID} /addfirst`).
* Do not format, encrypt or otherwise touch the hidden partition. It contains the
  assets and the board profile.

## Notes on the partition

| | |
|---|---|
| size | 512 MB by default (`-SizeMB`) |
| filesystem | FAT32 |
| GPT type | `c12a7328-f81f-11d2-ba4b-00a0c93ec93b` (EFI System Partition) |
| drive letter | removed after installation |
| GPT attributes | `0x8000000000000000` (no automount) |
| boot entry | `RTX30 UNLOCK`, device = that partition, path = `\EFI\BOOT\BOOTX64.EFI` |

The application accepts a boot volume if it is a USB device **or** a GPT ESP
(`GU_SSD_EXPERIMENT` build flag), which is why the partition uses the ESP type GUID.
