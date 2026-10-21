#pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>

#define GC_LIMIT 16
typedef struct {
  EFI_HANDLE handle, image;
  VOID *base;
  UINT64 bytes;
  EFI_DRIVER_BINDING_START start;
  EFI_DRIVER_BINDING_STOP stop;
  UINT32 version;
} GC_DRIVER;
typedef struct {
  EFI_HANDLE owner, children[GC_LIMIT], child_agents[GC_LIMIT];
  UINTN child_count, driver_count, physical_gops, text_outputs;
  GC_DRIVER drivers[GC_LIMIT];
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION mode;
} GC_SNAPSHOT;
typedef struct {
  EFI_SYSTEM_TABLE *table;
  BOOLEAN quiet;
  UINTN run_slot;
  EFI_HANDLE image, target;
  EFI_PCI_IO_PROTOCOL *pci;
  UINTN location[4];
  UINT32 identity, subsystem, class_revision;
  BOOLEAN (*log)(const char *format, ...);
} GC_CONTEXT;
typedef struct {
  BOOLEAN touched, restored;
  EFI_STATUS detach, isolated, claim, close, connect, verify;
} GC_RESULT;
EFI_STATUS GcCapture(GC_CONTEXT *context, GC_SNAPSHOT *snapshot);
BOOLEAN GcReady(const GC_SNAPSHOT *snapshot);
BOOLEAN GcSame(const GC_SNAPSHOT *a, const GC_SNAPSHOT *b);
EFI_STATUS GcCycle(GC_CONTEXT *context, const GC_SNAPSHOT *approved, GC_RESULT *result);
