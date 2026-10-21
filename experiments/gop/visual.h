#pragma once
#include "leaf.h"
EFI_STATUS GvClockInit(GC_CONTEXT *context);
BOOLEAN GvMark(GC_CONTEXT *context, const char *phase);
EFI_STATUS GvDisplay(GC_CONTEXT *context, const GL_PLAN *original, const GL_PLAN *fresh);
