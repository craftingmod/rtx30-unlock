#pragma once

#include "port.h"

typedef struct {
  UINT16 pm, pcie, msi, msix;
  UINT8 pcie_version;
} GU_CAPS;

/* Config is a complete, CPU-owned 256-byte snapshot. Out is cleared on failure. */
BOOLEAN GuCheckConfig(const UINT32 Config[64], UINTN Segment, UINTN Bus, UINTN Device,
                      UINTN Function, GU_CAPS *Out);

/* Path must point to readable memory through its terminator, up to MaxBytes.
   MaxBytes is a read bound, not necessarily the exact allocation length.
   Returns 1 for the Intel route, 2 for the RTX route, and 0 otherwise. */
int GuDisplayPath(const EFI_DEVICE_PATH_PROTOCOL *Path, UINTN MaxBytes);

/* Resolve GOP ownership to a distinct live PCI display function. No writes. */
BOOLEAN GuIndependentDisplay(EFI_BOOT_SERVICES *Bs, EFI_PCI_IO_PROTOCOL *Target,
                             EFI_HANDLE TargetHandle, EFI_DEVICE_PATH_PROTOCOL *Path);
