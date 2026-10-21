#include "tree.h"
#include <Protocol/DevicePath.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static EFI_GUID PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID, PciGuid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE Table;
static GC_CONTEXT Context;
static EFI_DEVICE_PATH_PROTOCOL Path;
static unsigned Nodes, Allocations, Visits, Calls, FailLog, FailNode, FailOpen;
static BOOLEAN Duplicate, Cycle, NullChild, BadCount, FreeError, NoPath;
static BOOLEAN SawGop, SawText, SawSeen, SawComplete;
static EFI_HANDLE H(unsigned index) { return (EFI_HANDLE)(UINTN)((index + 1) * 0x1000); }
static unsigned Index(EFI_HANDLE h) {
  unsigned i = (unsigned)((UINTN)h / 0x1000) - 1;
  assert(i < Nodes);
  return i;
}
static BOOLEAN Log(const char *format, ...) {
  Calls++;
  if (FailLog && Calls >= FailLog) return FALSE;
  if (!strcmp(format, "TREE_NODE handle=%#llx depth=%zu console_out=%u console_in=%u stderr=%u\n"))
    Visits++;
  va_list args;
  va_start(args, format);
  if (!strncmp(format, "TREE_PROTOCOL handle=", 21)) {
    (void)va_arg(args, unsigned long long);
    const char *role = va_arg(args, const char *);
    if (!strcmp(role, "GOP")) SawGop = TRUE;
    if (!strcmp(role, "TEXT_OUT")) SawText = TRUE;
  }
  if (!strncmp(format, "TREE_EDGE ", 10)) {
    for (unsigned i = 0; i < 3; i++)
      (void)va_arg(args, unsigned long long);
    (void)va_arg(args, unsigned);
    if (va_arg(args, int)) SawSeen = TRUE;
  }
  if (!strncmp(format, "TREE_END ", 9)) {
    (void)va_arg(args, unsigned long long);
    SawComplete = va_arg(args, int) != 0;
  }
  va_end(args);
  return TRUE;
}
static VOID *Alloc(UINTN size) {
  VOID *p = calloc(1, size);
  assert(p);
  Allocations++;
  return p;
}
static EFI_STATUS EFIAPI Free(VOID *p) {
  assert(p && Allocations);
  free(p);
  Allocations--;
  return FreeError ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *guid, VOID **out) {
  (void)h;
  *out = NULL;
  if (!memcmp(guid, &PathGuid, sizeof(*guid)) && !NoPath) {
    *out = &Path;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI Locate(EFI_GUID *guid, EFI_DEVICE_PATH_PROTOCOL **path,
                                EFI_HANDLE *handle) {
  assert(!memcmp(guid, &PciGuid, sizeof(*guid)) && *path == &Path);
  *handle = H(0);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h, EFI_GUID ***protocols, UINTN *count) {
  unsigned i = Index(h);
  *count = 0;
  *protocols = NULL;
  if (FailNode == i + 1) return EFI_ACCESS_DENIED;
  *count = 1;
  *protocols = Alloc(sizeof(**protocols));
  (*protocols)[0] = i == 0 ? &PathGuid : i == 1 ? &GopGuid : &TextGuid;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h, EFI_GUID *guid,
                               EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **entries, UINTN *count) {
  (void)guid;
  unsigned i = Index(h);
  *entries = NULL;
  *count = 0;
  if (FailOpen == i + 1) return EFI_DEVICE_ERROR;
  if (BadCount) {
    *count = 129;
    return EFI_SUCCESS;
  }
  BOOLEAN edge = i + 1 < Nodes || Cycle || NullChild;
  *count = edge ? (Duplicate ? 3 : 2) : 1;
  *entries = Alloc(*count * sizeof(**entries));
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e = *entries;
  e[0].AgentHandle = H(0);
  e[0].ControllerHandle = h;
  e[0].Attributes = EFI_OPEN_PROTOCOL_BY_DRIVER;
  e[0].OpenCount = 1;
  if (edge) {
    e[1].AgentHandle = H(0);
    e[1].ControllerHandle = NullChild ? NULL : i + 1 < Nodes ? H(i + 1) : H(0);
    e[1].Attributes = EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
    e[1].OpenCount = 1;
    if (Duplicate) e[2] = e[1];
  }
  return EFI_SUCCESS;
}
static VOID Reset(VOID) {
  assert(!Allocations);
  memset(&Bs, 0, sizeof(Bs));
  memset(&Table, 0, sizeof(Table));
  memset(&Context, 0, sizeof(Context));
  Nodes = 3;
  Visits = Calls = FailLog = FailNode = FailOpen = 0;
  Duplicate = Cycle = NullChild = BadCount = FreeError = NoPath = SawGop = SawText = SawSeen =
      SawComplete = FALSE;
  Bs.HandleProtocol = Handle;
  Bs.LocateDevicePath = Locate;
  Bs.ProtocolsPerHandle = Protocols;
  Bs.OpenProtocolInformation = Opens;
  Bs.FreePool = Free;
  Table.BootServices = &Bs;
  Context.table = &Table;
  Context.target = H(0);
  Context.log = Log;
  /* No Connect/Disconnect/Open/CloseProtocol, PCI writes, or GOP methods exist in this mock. */
}
int main(void) {
  Reset();
  assert(GcReadTree(&Context) == EFI_SUCCESS && Visits == 3 && SawGop && SawText && SawComplete);
  Reset();
  Duplicate = TRUE;
  assert(GcReadTree(&Context) == EFI_SUCCESS && Visits == 3 && SawSeen);
  Reset();
  Cycle = TRUE;
  assert(GcReadTree(&Context) == EFI_SUCCESS && Visits == 3 && SawSeen);
  Reset();
  NoPath = TRUE;
  assert(GcReadTree(&Context) == EFI_SUCCESS && Visits == 3 && SawComplete);
  Reset();
  Nodes = 10;
  assert(GcReadTree(&Context) == EFI_BUFFER_TOO_SMALL && !SawComplete && Visits == 9);
  Reset();
  NullChild = TRUE;
  assert(GcReadTree(&Context) == EFI_COMPROMISED_DATA && !SawComplete);
  Reset();
  BadCount = TRUE;
  assert(GcReadTree(&Context) == EFI_COMPROMISED_DATA && !SawComplete);
  Reset();
  FailNode = 2;
  assert(GcReadTree(&Context) == EFI_ACCESS_DENIED && !SawComplete);
  Reset();
  FailOpen = 2;
  assert(GcReadTree(&Context) == EFI_DEVICE_ERROR && !SawComplete);
  Reset();
  FreeError = TRUE;
  assert(GcReadTree(&Context) == EFI_DEVICE_ERROR && !SawComplete);
  Reset();
  FailLog = 1;
  assert(GcReadTree(&Context) == EFI_DEVICE_ERROR && !Visits);
  Reset();
  FailLog = 6;
  assert(GcReadTree(&Context) == EFI_DEVICE_ERROR);
  assert(!Allocations);
  puts("PASS 12 read-only ownership tree scenarios; mutating services unavailable");
  return 0;
}
