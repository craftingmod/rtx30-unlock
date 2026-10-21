#pragma once
#include "cycle.h"
#include <Protocol/SimpleFileSystem.h>
typedef struct {
  EFI_HANDLE image;
} GW_PLAN;
EFI_STATUS GwPrepare(GC_CONTEXT *context, GW_PLAN *plan);
EFI_STATUS GwCancel(GC_CONTEXT *context, GW_PLAN *plan);
EFI_STATUS GwStart(GC_CONTEXT *context, GW_PLAN *plan, EFI_FILE_PROTOCOL **report,
                   BOOLEAN verified);
