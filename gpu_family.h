#pragma once
#include <Uefi.h>

/* RTX 30 PCI families are candidates only; PMC_BOOT_0 decides the actual chip. */
static inline BOOLEAN GpuGa10xPciCandidate(UINT32 Identity) {
  UINT32 Family = (Identity >> 24) & 0xffU;
  return (Identity & 0xffffU) == 0x10deU && (Family == 0x22U || Family == 0x24U || Family == 0x25U);
}

/* PMC_BOOT_0 fields and chip IDs follow Linux nouveau/nova device identification. */
static inline BOOLEAN GpuGa10xChip(UINT32 Boot0) {
  switch ((Boot0 >> 20) & 0x1ffU) {
  case 0x172:
  case 0x173:
  case 0x174:
  case 0x176:
  case 0x177:
    return TRUE;
  default:
    return FALSE;
  }
}
