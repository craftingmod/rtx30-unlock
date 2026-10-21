#include "cycle.h"
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextOut.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TARGET ((EFI_HANDLE)(UINTN)0x1000)
#define CHILD ((EFI_HANDLE)(UINTN)0x2000)
#define NEW_CHILD ((EFI_HANDLE)(UINTN)0x2100)
#define OWNER ((EFI_HANDLE)(UINTN)0x3000)
#define TEXT_DRIVER ((EFI_HANDLE)(UINTN)0x4000)
#define APP ((EFI_HANDLE)(UINTN)0x5000)
static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID, GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                BindingGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID,
                LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID,
                TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE Table;
static EFI_PCI_IO_PROTOCOL Pci, Replacement;
static EFI_DRIVER_BINDING_PROTOCOL Binding[2];
static EFI_LOADED_IMAGE_PROTOCOL Loaded;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Text;
static EFI_DEVICE_PATH_PROTOCOL Path;
static GC_CONTEXT Context;
static BOOLEAN Detached, Partial, Unknown, Nested, Foreign, Changed, LogFault, FailLogAfterDetach,
    Reappeared;
static BOOLEAN ChangePci, ChangeClaim, Recreated, Exclusive, FailLogBefore;
static BOOLEAN PathChild, BothChildren, ConflictingChild, MissingOwner, MissingChildPath,
    UnknownChildProtocol;
static EFI_STATUS DetachStatus, ClaimStatus, CloseStatus, ConnectStatus;
static unsigned Detaches, Connects, Claims, Closes, Scans, Allocations;
static UINT32 Identity, Subsystem, ClassRevision;
static BOOLEAN Eq(EFI_GUID *a, EFI_GUID *b) { return !memcmp(a, b, sizeof(*a)); }
static EFI_HANDLE Child(VOID) { return Recreated && Connects ? NEW_CHILD : CHILD; }
static VOID *Alloc(UINTN size) {
  VOID *p = calloc(1, size);
  assert(p);
  Allocations++;
  return p;
}
static EFI_STATUS EFIAPI Free(VOID *p) {
  assert(p && Allocations);
  Allocations--;
  free(p);
  return EFI_SUCCESS;
}
static BOOLEAN Log(const char *format, ...) {
  (void)format;
  if (FailLogBefore || (FailLogAfterDetach && Detaches)) LogFault = TRUE;
  return !LogFault;
}
static EFI_STATUS EFIAPI Start(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h,
                               EFI_DEVICE_PATH_PROTOCOL *p) {
  (void)b;
  (void)h;
  (void)p;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Stop(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h, UINTN n,
                              EFI_HANDLE *p) {
  (void)b;
  (void)h;
  (void)n;
  (void)p;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *t, CHAR16 *s) {
  (void)t;
  (void)s;
  assert(!"cycle must never call a console");
  return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *p, UINTN *s, UINTN *b, UINTN *d, UINTN *f) {
  (void)p;
  *s = 0;
  *b = 1;
  *d = 0;
  *f = 0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Read(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT32 offset,
                              UINTN count, VOID *data) {
  (void)p;
  assert(w == EfiPciIoWidthUint32 && count == 1);
  assert(offset == 0 || offset == 0x2c || offset == 8);
  *(UINT32 *)data = offset == 0 ? Identity : offset == 8 ? ClassRevision : Subsystem;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *guid, VOID **out) {
  *out = NULL;
  if (h == TARGET && Eq(guid, &PciGuid)) {
    *out = ChangePci && Detaches ? &Replacement : &Pci;
    return EFI_SUCCESS;
  }
  if ((h == TARGET || h == Child()) && Eq(guid, &PathGuid)) {
    if (h == Child() && MissingChildPath) return EFI_NOT_FOUND;
    *out = &Path;
    return EFI_SUCCESS;
  }
  if (h == Child() && !Detached && Eq(guid, &GopGuid)) {
    *out = &Gop;
    return EFI_SUCCESS;
  }
  if (h == Child() && !Detached && Eq(guid, &TextGuid)) {
    *out = &Text;
    return EFI_SUCCESS;
  }
  if ((h == OWNER || h == TEXT_DRIVER) && Eq(guid, &BindingGuid)) {
    *out = &Binding[h == TEXT_DRIVER];
    return EFI_SUCCESS;
  }
  if ((h == OWNER || h == TEXT_DRIVER) && Eq(guid, &LoadedGuid)) {
    *out = &Loaded;
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h, EFI_GUID ***out, UINTN *count) {
  assert(h == TARGET || h == Child());
  *count = h == TARGET && !PathChild ? 1 : 2;
  *out = Alloc(*count * sizeof(**out));
  if (h == TARGET) {
    (*out)[0] = PathChild ? (UnknownChildProtocol ? &TextGuid : &PathGuid) : &PciGuid;
    if (*count == 2) (*out)[1] = &PciGuid;
  } else {
    (*out)[0] = &GopGuid;
    (*out)[1] = &TextGuid;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h, EFI_GUID *guid,
                               EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out, UINTN *count) {
  *count = 0;
  *out = NULL;
  if (Detached) return EFI_SUCCESS;
  if (h == TARGET) {
    *count = Eq(guid, &PciGuid) ? (MissingOwner ? 0 : PathChild && !BothChildren ? 1 : 2) : 1;
  } else if (Eq(guid, &GopGuid)) {
    *count = 1;
  }
  if (!*count) return EFI_SUCCESS;
  *out = Alloc(*count * sizeof(**out));
  (*out)[0].AgentHandle = h == TARGET ? OWNER : TEXT_DRIVER;
  (*out)[0].ControllerHandle = h;
  (*out)[0].Attributes = EFI_OPEN_PROTOCOL_BY_DRIVER;
  (*out)[0].OpenCount = 1;
  if (h == TARGET) {
    if (!Eq(guid, &PciGuid) || *count == 2) {
      UINTN at = Eq(guid, &PciGuid) ? 1 : 0;
      (*out)[at].AgentHandle =
          Unknown || (ConflictingChild && !Eq(guid, &PciGuid)) ? TEXT_DRIVER : OWNER;
      (*out)[at].ControllerHandle = Child();
      (*out)[at].Attributes = EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
      (*out)[at].OpenCount = 1;
    }
  } else if (Nested)
    (*out)[0].Attributes = EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Locate(EFI_GUID *guid, EFI_DEVICE_PATH_PROTOCOL **path, EFI_HANDLE *h) {
  assert(Eq(guid, &PciGuid) && *path == &Path);
  *h = Foreign ? (EFI_HANDLE)(UINTN)0x9999 : TARGET;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handles(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *guid, VOID *key,
                                 UINTN *count, EFI_HANDLE **out) {
  (void)key;
  assert(type == ByProtocol && Eq(guid, &GopGuid));
  Scans++;
  *out = NULL;
  *count = 0;
  if (Detached) return EFI_NOT_FOUND;
  *count = 1;
  *out = Alloc(sizeof(**out));
  (*out)[0] = Child();
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Disconnect(EFI_HANDLE h, EFI_HANDLE driver, EFI_HANDLE child) {
  assert(h == TARGET && driver == OWNER && !child && !Exclusive);
  Detaches++;
  if (!Partial) {
    Detached = TRUE;
  }
  if (Reappeared) Detached = FALSE;
  return DetachStatus;
}
static EFI_STATUS EFIAPI Connect(EFI_HANDLE h, EFI_HANDLE *drivers, EFI_DEVICE_PATH_PROTOCOL *path,
                                 BOOLEAN recursive) {
  assert(h == TARGET && drivers[0] == OWNER && drivers[1] == TEXT_DRIVER && !drivers[2] && !path &&
         recursive && !Exclusive);
  Connects++;
  if (!EFI_ERROR(ConnectStatus)) Detached = FALSE;
  if (Changed) Info.HorizontalResolution = 800;
  return ConnectStatus;
}
static EFI_STATUS EFIAPI Open(EFI_HANDLE h, EFI_GUID *guid, VOID **out, EFI_HANDLE agent,
                              EFI_HANDLE controller, UINT32 attr) {
  assert(h == TARGET && Eq(guid, &PciGuid) && agent == APP && !controller &&
         attr == EFI_OPEN_PROTOCOL_EXCLUSIVE && Detached);
  Claims++;
  if (!EFI_ERROR(ClaimStatus)) {
    Exclusive = TRUE;
    *out = ChangeClaim ? &Replacement : Context.pci;
  }
  return ClaimStatus;
}
static EFI_STATUS EFIAPI Close(EFI_HANDLE h, EFI_GUID *guid, EFI_HANDLE agent,
                               EFI_HANDLE controller) {
  assert(h == TARGET && Eq(guid, &PciGuid) && agent == APP && !controller && Exclusive);
  Closes++;
  if (!EFI_ERROR(CloseStatus)) Exclusive = FALSE;
  return CloseStatus;
}
static VOID Reset(VOID) {
  assert(!Allocations);
  memset(&Bs, 0, sizeof(Bs));
  memset(&Table, 0, sizeof(Table));
  memset(&Context, 0, sizeof(Context));
  memset(&Pci, 0, sizeof(Pci));
  memset(&Info, 0, sizeof(Info));
  memset(&Mode, 0, sizeof(Mode));
  memset(&Gop, 0, sizeof(Gop));
  memset(Binding, 0, sizeof(Binding));
  memset(&Loaded, 0, sizeof(Loaded));
  memset(&Text, 0, sizeof(Text));
  Detached = Partial = Unknown = Nested = Foreign = Changed = LogFault = FailLogAfterDetach =
      Reappeared = FALSE;
  ChangePci = ChangeClaim = Recreated = Exclusive = FailLogBefore = FALSE;
  PathChild = BothChildren = ConflictingChild = MissingOwner = MissingChildPath =
      UnknownChildProtocol = FALSE;
  DetachStatus = ClaimStatus = CloseStatus = ConnectStatus = EFI_SUCCESS;
  Detaches = Connects = Claims = Closes = Scans = 0;
  Bs.HandleProtocol = Handle;
  Bs.ProtocolsPerHandle = Protocols;
  Bs.OpenProtocolInformation = Opens;
  Bs.FreePool = Free;
  Bs.LocateDevicePath = Locate;
  Bs.LocateHandleBuffer = Handles;
  Bs.DisconnectController = Disconnect;
  Bs.ConnectController = Connect;
  Bs.OpenProtocol = Open;
  Bs.CloseProtocol = Close;
  Table.BootServices = &Bs;
  Pci.Pci.Read = Read;
  Pci.GetLocation = Location;
  Replacement = Pci;
  for (UINTN i = 0; i < 2; i++) {
    Binding[i].DriverBindingHandle = Binding[i].ImageHandle = i ? TEXT_DRIVER : OWNER;
    Binding[i].Start = Start;
    Binding[i].Stop = Stop;
  }
  Loaded.ImageBase = &Loaded;
  Loaded.ImageSize = 4096;
  Loaded.ImageCodeType = EfiBootServicesCode;
  Info.HorizontalResolution = 1024;
  Info.VerticalResolution = 768;
  Info.PixelsPerScanLine = 1024;
  Info.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Mode.Info = &Info;
  Mode.SizeOfInfo = sizeof(Info);
  Mode.FrameBufferBase = 0x80000000;
  Mode.FrameBufferSize = 1024 * 768 * 4;
  Mode.MaxMode = 1;
  Gop.Mode = &Mode;
  Text.OutputString = Output;
  Context.table = &Table;
  Context.image = APP;
  Context.target = TARGET;
  Context.pci = &Pci;
  Context.location[1] = 1;
  Context.log = Log;
  Context.identity = Identity = 0x220610deU;
  Context.subsystem = Subsystem = 0x38901462U;
  Context.class_revision = ClassRevision = 0x030000a1U;
}
static GC_SNAPSHOT Approved(VOID) {
  GC_SNAPSHOT s;
  assert(GcCapture(&Context, &s) == EFI_SUCCESS && GcReady(&s));
  assert(!Allocations);
  return s;
}
int main(void) {
  GC_RESULT result;
  GC_SNAPSHOT s;
  Reset();
  s = Approved();
  assert(GcCycle(&Context, &s, &result) == EFI_SUCCESS && result.restored);
  assert(Detaches == 1 && Claims == 1 && Closes == 1 && Connects == 1 && !Exclusive &&
         !Allocations);
  Reset();
  s = Approved();
  Recreated = TRUE;
  assert(GcCycle(&Context, &s, &result) == EFI_SUCCESS && result.restored);
  Reset();
  s = Approved();
  ChangePci = TRUE;
  assert(GcCycle(&Context, &s, &result) == EFI_SUCCESS && Context.pci == &Replacement);
  Reset();
  s = Approved();
  ChangeClaim = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && Closes == 1 && Connects == 1 &&
         result.restored);
  Reset();
  s = Approved();
  Unknown = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !result.touched && !Detaches);
  Reset();
  s = Approved();
  Nested = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Detaches);
  Reset();
  s = Approved();
  Foreign = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Detaches);
  Reset();
  s = Approved();
  Binding[0].Version++;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Detaches);
  Reset();
  s = Approved();
  FailLogBefore = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Detaches);
  Reset();
  s = Approved();
  Partial = TRUE;
  DetachStatus = EFI_DEVICE_ERROR;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Claims && Connects == 1 && result.restored);
  Reset();
  s = Approved();
  Reappeared = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Claims && Connects == 1 && result.restored);
  Reset();
  s = Approved();
  ClaimStatus = EFI_ACCESS_DENIED;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Closes && Connects == 1 && result.restored);
  Reset();
  s = Approved();
  CloseStatus = EFI_DEVICE_ERROR;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && Exclusive && !Connects && !result.restored);
  Reset();
  s = Approved();
  ConnectStatus = EFI_DEVICE_ERROR;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !result.restored && Connects == 1);
  Reset();
  s = Approved();
  Changed = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !result.restored && Connects == 1);
  Reset();
  s = Approved();
  FailLogAfterDetach = TRUE;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && Connects == 1 && !result.restored);
  assert(!Allocations);
  const UINT32 ids[] = {0x250310deU, 0x220410deU, 0x278610deU};
  for (UINTN i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
    Reset();
    Context.identity = Identity = ids[i];
    Context.subsystem = Subsystem = 0xabcd1234U;
    s = Approved();
    assert(GcCycle(&Context, &s, &result) == EFI_SUCCESS && result.restored);
  }
  Reset();
  s = Approved();
  Identity = 0x278610deU;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Detaches);
  Reset();
  s = Approved();
  Subsystem++;
  assert(EFI_ERROR(GcCycle(&Context, &s, &result)) && !Detaches);
  Reset();
  Context.identity = Identity = 0x12341002U;
  assert(EFI_ERROR(GcCapture(&Context, &s)) && !Detaches);
  assert(!Allocations);
  /* Reproduce the reported Device Path child edge before the PCI I/O owner. */
  Reset();
  PathChild = TRUE;
  Context.identity = Identity = 0x220310deU;
  Context.subsystem = Subsystem = 0x20007377U;
  s = Approved();
  assert(s.child_count == 1 && s.child_agents[0] == OWNER);
  assert(GcCycle(&Context, &s, &result) == EFI_SUCCESS && result.restored && Detaches == 1 &&
         Connects == 1);
  Reset();
  PathChild = BothChildren = TRUE;
  s = Approved();
  assert(s.child_count == 1);
  assert(GcCycle(&Context, &s, &result) == EFI_SUCCESS && result.restored);
  Reset();
  PathChild = TRUE;
  s = Approved();
  Unknown = TRUE;
  assert(GcCycle(&Context, &s, &result) == EFI_ACCESS_DENIED && !Detaches && !result.touched);
  Reset();
  PathChild = BothChildren = ConflictingChild = TRUE;
  assert(GcCapture(&Context, &s) == EFI_ACCESS_DENIED && !Detaches);
  Reset();
  PathChild = MissingOwner = TRUE;
  assert(GcCapture(&Context, &s) == EFI_ACCESS_DENIED && !Detaches);
  Reset();
  PathChild = Foreign = TRUE;
  assert(GcCapture(&Context, &s) == EFI_ACCESS_DENIED && !Detaches);
  Reset();
  PathChild = MissingChildPath = TRUE;
  assert(GcCapture(&Context, &s) == EFI_NOT_FOUND && !Detaches);
  Reset();
  PathChild = Nested = TRUE;
  assert(GcCapture(&Context, &s) == EFI_ACCESS_DENIED && !Detaches);
  Reset();
  PathChild = UnknownChildProtocol = TRUE;
  assert(GcCapture(&Context, &s) == EFI_ACCESS_DENIED && !Detaches);
  assert(!Allocations);
  puts("PASS 31 GOP ownership/recovery/device-identity scenarios; no GPU or console access");
  return 0;
}
