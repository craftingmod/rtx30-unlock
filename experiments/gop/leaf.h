#pragma once
#include "cycle.h"
#define GL_HANDLES 8
#define GL_RECORDS 48
typedef struct {
  EFI_HANDLE handle, agent, controller;
  EFI_GUID protocol;
  UINT32 attributes, count;
} GL_RECORD;
typedef struct {
  GL_RECORD records[GL_RECORDS];
  UINTN count;
  UINT32 member;
} GL_GUARD;
typedef struct {
  EFI_HANDLE physical, text, shared, handles[GL_HANDLES];
  UINTN handle_count;
  EFI_HANDLE console_in, console_out, stderr_handle;
  VOID *input, *output, *stderr_protocol;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
  EFI_PHYSICAL_ADDRESS framebuffer;
  UINTN framebuffer_size;
  UINT32 mode;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text_protocol;
  GC_DRIVER driver;
  EFI_DRIVER_BINDING_SUPPORTED supported;
  GL_GUARD guard;
} GL_PLAN;
EFI_STATUS GcLeafPrepare(GC_CONTEXT *context, GL_PLAN *plan);
EFI_STATUS GcLeafRun(GC_CONTEXT *context, const GL_PLAN *plan, GC_RESULT *result);
EFI_STATUS GcLeafGuard(GC_CONTEXT *context, const GL_PLAN *plan, GL_GUARD *guard,
                       BOOLEAN shared_only);
BOOLEAN GcLeafSameGuard(const GL_GUARD *a, const GL_GUARD *b);
EFI_STATUS GcLeafPair(GC_CONTEXT *context, EFI_HANDLE handle, EFI_GUID *protocol, EFI_HANDLE *agent,
                      EFI_HANDLE *child);
