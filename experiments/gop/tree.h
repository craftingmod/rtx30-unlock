#pragma once
#include "cycle.h"
/* Collects protocol relationships only; never opens exclusive access or changes drivers. */
EFI_STATUS GcReadTree(GC_CONTEXT *context);
