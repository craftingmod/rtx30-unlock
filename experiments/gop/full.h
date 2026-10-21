#pragma once
#include "leaf.h"
#include "reset.h"
typedef struct {
  GC_DRIVER driver;
  EFI_DRIVER_BINDING_SUPPORTED supported;
  BOOLEAN runtime;
} GF_DRIVER;
typedef struct {
  GL_PLAN leaf;
  GL_GUARD shared;
  GF_PCI pci;
  GF_DRIVER drivers[6]; /* vendor, bridge, aggregate GOP, text member, two EDID consumers */
  EFI_HANDLE sibling;
} GF_PLAN;
EFI_STATUS GfPrepare(GC_CONTEXT *context, GF_PLAN *plan);
EFI_STATUS GfRun(GC_CONTEXT *context, const GF_PLAN *plan, GC_RESULT *result);
