#pragma once
#include "cycle.h"
typedef struct {
  UINT32 config[64];
  UINT16 pcie, control, control2;
  UINT8 version;
} GF_PCI;
EFI_STATUS GfSavePci(GC_CONTEXT *context, GF_PCI *saved);
EFI_STATUS GfResetPci(GC_CONTEXT *context, const GF_PCI *saved, BOOLEAN *attempted);
