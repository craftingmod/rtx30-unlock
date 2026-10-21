#pragma once
#include "cycle.h"
EFI_STATUS GxPrepare(GC_CONTEXT *context);
EFI_STATUS GxPatch(BOOLEAN (*isolated)(VOID));
BOOLEAN GxSafe(VOID);
VOID GxReleased(VOID);
EFI_STATUS GxObserve(GC_CONTEXT *context,const char *phase);
EFI_STATUS GxFinish(VOID);

#ifdef GU_SSD_EXPERIMENT
BOOLEAN GxAlreadyApplied(VOID);
EFI_STATUS GxInitialSelectors(GC_CONTEXT *context,BOOLEAN *skip);
#endif
