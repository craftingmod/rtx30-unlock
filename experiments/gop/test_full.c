/* Reuse the validated five-handle fixture, then model complete teardown and rejoin. */
#define main LeafRegressionMain
#include "test_leaf.c"
#undef main
#include "full.h"
#include <stdarg.h>
#include <Protocol/EdidDiscovered.h>
static EFI_DRIVER_BINDING_PROTOCOL Fb[5];
static EFI_LOADED_IMAGE_PROTOCOL Fi[5];
static BOOLEAN VendorOn, ChildrenOn, BridgeOn, BridgeChild, AggOn, EdidP, EdidS, Exclusive,
    NewHandles, Recreate;
static BOOLEAN Quiet, FailResetWrite, FailRestore, Pending, IdentityTimeout, FailLogRun,
    ChangeRuntimeInput;
static BOOLEAN ExposeGop, UnexpectedMaster, FailClose, ResidualOwner, PathlessGop, FailIsolationLog;
static EFI_GRAPHICS_OUTPUT_PROTOCOL VirtualGop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE VirtualMode;
static BOOLEAN ResidualVirtual, HideVirtual, FailVirtualLog;
static UINT32 VirtualOwnerAttr;
static char FullLog[65536];
static size_t FullLogUsed;
static unsigned FailStopRole, FailStartRole, FullStops, FullStarts, Flrs, QuietWaits, CloseCount,
    WriteCount;
static UINT32 Config[64];
static UINT16 Fpcie;
#ifdef GC_PATCH_FIXTURE
static BOOLEAN FailChildLog;
#endif
static EFI_HANDLE Original(EFI_HANDLE h) {
  return NewHandles && (h == H(102) || h == H(103) || h == H(105)) ? H((UINTN)h - 100) : h;
}
static EFI_HANDLE Current(EFI_HANDLE h) {
  return NewHandles && (h == PHYS || h == TEXT || h == EDID) ? H((UINTN)h + 100) : h;
}
static unsigned Actor(EFI_HANDLE h) {
  if (h == VENDOR) return 0;
  if (h == BRIDGE) return 1;
  if (h == H(15)) return 2;
  if (h == MEMBER) return 3;
  assert(h == RUNTIME);
  return 4;
}
static EFI_STATUS EFIAPI FRead(EFI_PCI_IO_PROTOCOL *pci, EFI_PCI_IO_PROTOCOL_WIDTH width,
                               UINT32 offset, UINTN count, VOID *data) {
  assert(pci == &Pci && !Quiet);
  UINTN bytes = width == EfiPciIoWidthUint8 ? 1 : width == EfiPciIoWidthUint16 ? 2 : 4;
  assert(width <= EfiPciIoWidthUint32 && offset + bytes * count <= sizeof(Config));
  memcpy(data, (UINT8 *)Config + offset, bytes * count);
  if (IdentityTimeout && Flrs && offset == 0 && count == 1) *(UINT32 *)data = 0xffffffffU;
  if (Pending && offset == Fpcie + 10 && count == 1) *(UINT16 *)data |= 0x20;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FWrite(EFI_PCI_IO_PROTOCOL *pci, EFI_PCI_IO_PROTOCOL_WIDTH width,
                                UINT32 offset, UINTN count, VOID *data) {
  assert(pci == &Pci && !Quiet && Exclusive && count == 1);
  UINTN bytes = width == EfiPciIoWidthUint8 ? 1 : width == EfiPciIoWidthUint16 ? 2 : 4;
  assert(offset + bytes <= sizeof(Config));
  WriteCount++;
  if (offset == Fpcie + 8 && width == EfiPciIoWidthUint16 && (*(UINT16 *)data & 0x8000)) {
    assert(!VendorOn && !ChildrenOn && !BridgeOn && !AggOn && !EdidP && !EdidS && !Bits && !Flrs &&
           !(Config[1] & 4));
    Flrs++;
    Quiet = TRUE;
    Config[1] &= ~7U;
    if (UnexpectedMaster) Config[1] |= 4;
    for (unsigned i = 4; i <= 9; i++)
      Config[i] = 0;
    return FailResetWrite ? EFI_DEVICE_ERROR : EFI_SUCCESS;
  }
  assert(!(offset == 4 && width == EfiPciIoWidthUint16 && (*(UINT16 *)data & 4)));
  if (FailRestore && Flrs) return EFI_DEVICE_ERROR;
  memcpy((UINT8 *)Config + offset, data, bytes);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Attr(EFI_PCI_IO_PROTOCOL *pci,
                              EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION operation, UINT64 attributes,
                              UINT64 *result) {
  (void)result;
  assert(pci == &Pci && Exclusive && !Quiet && !(attributes & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER));
  if (operation == EfiPciIoAttributeOperationDisable)
    Config[1] &= ~3U;
  else {
    assert(operation == EfiPciIoAttributeOperationEnable);
    Config[1] |= 2;
    if (attributes & EFI_PCI_IO_ATTRIBUTE_IO) Config[1] |= 1;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Stall(UINTN us) {
  if (Quiet) {
    assert(us == 100000);
    Quiet = FALSE;
    QuietWaits++;
  } else
    assert(us == 1000);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Pool(EFI_MEMORY_TYPE type, UINTN bytes, VOID **out) {
  assert(type == EfiLoaderData);
  *out = Alloc(bytes);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FHandle(EFI_HANDLE h, EFI_GUID *g, VOID **out) {
  *out = NULL;
  EFI_HANDLE actors[] = {VENDOR, BRIDGE, H(15), MEMBER, RUNTIME};
  for (unsigned i = 0; i < 5; i++) {
    if (h == actors[i] && Eq(g, &BindingGuid)) {
      *out = &Fb[i];
      return EFI_SUCCESS;
    }
    if (h == H(200 + i) && Eq(g, &LoadedGuid)) {
      *out = &Fi[i];
      return EFI_SUCCESS;
    }
  }
  if (h == ROOT && Eq(g, &PciGuid))
    *out = &Pci;
  else if (h == Current(PHYS) && ChildrenOn && Eq(g, &PathGuid))
    *out = &Path;
  else if (h == Current(PHYS) && (ChildrenOn || ExposeGop) && Eq(g, &GopGuid))
    *out = &Gop;
  else if (h == TEXT && ResidualVirtual && !ChildrenOn && Eq(g, &GopGuid))
    *out = &VirtualGop;
  else if (h == Current(TEXT) && ChildrenOn && BridgeChild && Eq(g, &TextGuid))
    *out = &LocalText;
  else if (h == SHARED && PathlessGop && Eq(g, &GopGuid))
    *out = &VirtualGop;
  else if (h == SHARED && Eq(g, &TextGuid))
    *out = &SharedText;
  else if (h == SHARED && Eq(g, &InputGuid))
    *out = &Input;
  return *out ? EFI_SUCCESS : EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI FHandles(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *g, VOID *key, UINTN *n,
                                  EFI_HANDLE **out) {
  (void)key;
  assert(type == ByProtocol && Eq(g, &GopGuid));
  *out = NULL;
  *n = 0;
  if (!ChildrenOn && !PathlessGop && (!ResidualVirtual || HideVirtual)) return EFI_NOT_FOUND;
  *n = 1;
  *out = Alloc(sizeof(**out));
  (*out)[0] = ChildrenOn ? Current(PHYS) : ResidualVirtual ? TEXT : SHARED;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FProtocols(EFI_HANDLE h, EFI_GUID ***out, UINTN *n) {
  EFI_HANDLE old = Original(h);
  if ((old == PHYS || old == EDID) && !ChildrenOn) {
    *out = NULL;
    *n = 0;
    return EFI_NOT_FOUND;
  }
  if (old == TEXT && !BridgeChild && !(ResidualVirtual && !ChildrenOn)) {
    *out = NULL;
    *n = 0;
    return EFI_NOT_FOUND;
  }
  return Protocols(old, out, n);
}
static EFI_STATUS EFIAPI FOpens(EFI_HANDLE h, EFI_GUID *g,
                                EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out, UINTN *n) {
  EFI_HANDLE old = Original(h);
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY e[4] = {0};
  *n = 0;
  if (old == ROOT && Eq(g, &PciGuid)) {
    if (VendorOn) Add(e, n, VENDOR, ROOT, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (Exclusive) Add(e, n, Context.image, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if (ResidualOwner && !VendorOn) Add(e, n, H(99), ROOT, EFI_OPEN_PROTOCOL_BY_DRIVER);
  } else if (old == ROOT && Eq(g, &PathGuid) && ChildrenOn) {
    Add(e, n, VENDOR, Current(PHYS), EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
    Add(e, n, VENDOR, Current(EDID), EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  } else if (old == PHYS && Eq(g, &GopGuid)) {
    if (BridgeOn) Add(e, n, BRIDGE, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (BridgeChild) Add(e, n, BRIDGE, Current(TEXT), EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  } else if (old == TEXT && Eq(g, &TextGuid)) {
    if (Bits & 1) Add(e, n, MEMBER, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (Bits & 2) Add(e, n, MEMBER, SHARED, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  } else if (old == TEXT && Eq(g, &GopGuid)) {
    if (AggOn) Add(e, n, H(15), h, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (ResidualVirtual && !VendorOn && VirtualOwnerAttr)
      Add(e, n, H(99), SHARED, VirtualOwnerAttr);
  } else if (old == PHYS && Eq(g, &EdidGuid) && EdidP)
    Add(e, n, RUNTIME, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
  else if (old == EDID && Eq(g, &EdidGuid) && EdidS)
    Add(e, n, RUNTIME, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
  else if (old == SHARED && Eq(g, &InputGuid))
    Add(e, n, GuardChanged ? VENDOR : RUNTIME, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
  *out = NULL;
  if (*n) {
    *out = Alloc(*n * sizeof(**out));
    memcpy(*out, e, *n * sizeof(**out));
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FStop(EFI_DRIVER_BINDING_PROTOCOL *binding, EFI_HANDLE parent, UINTN n,
                               EFI_HANDLE *children) {
  unsigned role = Actor(binding->DriverBindingHandle);
  EFI_HANDLE old = Original(parent);
  FullStops++;
  assert(!Exclusive && !Flrs);
  if (FailStopRole == role + 1) return EFI_DEVICE_ERROR;
  if (role == 3) {
    assert(old == TEXT && n == 1 && children[0] == SHARED && Bits == 3);
    Bits = 0;
  } else if (role == 2) {
    assert(old == TEXT && !n && !Bits && AggOn);
    AggOn = FALSE;
  } else if (role == 1) {
    assert(old == PHYS && !Bits && !AggOn);
    if (n) {
      assert(n == 1 && children[0] == TEXT && BridgeChild);
      BridgeChild = FALSE;
    } else {
      assert(!BridgeChild);
      BridgeOn = FALSE;
    }
  } else if (role == 4) {
    assert(!n && !BridgeOn && !BridgeChild && (old == PHYS || old == EDID));
    if (old == PHYS)
      EdidP = FALSE;
    else
      EdidS = FALSE;
    if (ChangeRuntimeInput) GuardChanged = TRUE;
  } else {
    assert(role == 0 && old == ROOT && !Bits && !BridgeOn && !AggOn && !EdidP && !EdidS);
    if (n) {
      assert(n == 2 && children[0] == PHYS && children[1] == EDID);
      ChildrenOn = FALSE;
    } else {
      assert(!ChildrenOn);
      VendorOn = FALSE;
    }
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FSupported(EFI_DRIVER_BINDING_PROTOCOL *binding, EFI_HANDLE parent,
                                    EFI_DEVICE_PATH_PROTOCOL *path) {
  (void)parent;
  assert(!path && !Exclusive && Flrs == 1);
  unsigned role = Actor(binding->DriverBindingHandle);
  return FailStartRole == role + 1 ? EFI_UNSUPPORTED : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FStart(EFI_DRIVER_BINDING_PROTOCOL *binding, EFI_HANDLE parent,
                                EFI_DEVICE_PATH_PROTOCOL *path) {
  assert(!path && !Exclusive && Flrs == 1);
  unsigned role = Actor(binding->DriverBindingHandle);
  EFI_HANDLE old = Original(parent);
  FullStarts++;
  if (role == 0) {
    assert(old == ROOT && !VendorOn);
    VendorOn = ChildrenOn = TRUE;
    if (Recreate) NewHandles = TRUE;
  } else if (role == 4) {
    if (old == PHYS)
      EdidP = TRUE;
    else {
      assert(old == EDID);
      EdidS = TRUE;
    }
  } else if (role == 1) {
    assert(old == PHYS);
    BridgeOn = BridgeChild = TRUE;
  } else if (role == 2) {
    assert(old == TEXT && BridgeChild);
    AggOn = TRUE;
  } else {
    assert(role == 3 && old == TEXT && AggOn);
    Bits = 3;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ExclusiveOpen(EFI_HANDLE h, EFI_GUID *g, VOID **out, EFI_HANDLE agent,
                                       EFI_HANDLE controller, UINT32 attr) {
  assert(h == ROOT && Eq(g, &PciGuid) && agent == Context.image && !controller &&
         attr == EFI_OPEN_PROTOCOL_EXCLUSIVE && !VendorOn && !ChildrenOn);
  assert(!Exclusive);
  Exclusive = TRUE;
  *out = &Pci;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ExclusiveClose(EFI_HANDLE h, EFI_GUID *g, EFI_HANDLE agent,
                                        EFI_HANDLE controller) {
  assert(h == ROOT && Eq(g, &PciGuid) && agent == Context.image && !controller && Exclusive &&
         !Quiet);
  CloseCount++;
  if (FailClose) return EFI_DEVICE_ERROR;
  Exclusive = FALSE;
  return EFI_SUCCESS;
}
static BOOLEAN FLog(const char *format, ...) {
#ifdef GC_PATCH_FIXTURE
  if (FailChildLog && FullStarts && strstr(format, "CHILD_")) return FALSE;
#endif
  va_list args;
  va_start(args, format);
  int n = vsnprintf(FullLog + FullLogUsed, sizeof(FullLog) - FullLogUsed, format, args);
  va_end(args);
  assert(n >= 0 && (size_t)n < sizeof(FullLog) - FullLogUsed);
  FullLogUsed += (size_t)n;
  return !(FailLogRun && FullStops) &&
         !(FailIsolationLog && strstr(format, "FULL_OLD_INTERFACE")) &&
         !(FailVirtualLog && strstr(format, "FULL_VIRTUAL_GOP"));
}
static VOID FReset(VOID) {
  Reset();
  EdidGuid = (EFI_GUID)EFI_EDID_DISCOVERED_PROTOCOL_GUID;
  VendorOn = ChildrenOn = BridgeOn = BridgeChild = AggOn = EdidP = EdidS = TRUE;
  Exclusive = NewHandles = Recreate = Quiet = FailResetWrite = FailRestore = Pending =
      IdentityTimeout = FailLogRun = ChangeRuntimeInput = FALSE;
  ExposeGop = UnexpectedMaster = FailClose = ResidualOwner = PathlessGop = FailIsolationLog = FALSE;
  FullLogUsed = 0;
  FullLog[0] = 0;
  VirtualMode = Mode;
  VirtualGop = Gop;
  VirtualGop.Mode = &VirtualMode;
  ResidualVirtual = HideVirtual = FailVirtualLog = FALSE;
  VirtualOwnerAttr = 0;
  FailStopRole = FailStartRole = FullStops = FullStarts = Flrs = QuietWaits = CloseCount =
      WriteCount = 0;
  memset(Config, 0, sizeof(Config));
  Context.identity = Config[0] = 0x220310deU;
  Context.subsystem = Config[11] = 0x20007377U;
  Context.class_revision = Config[2] = 0x030000a1U;
  Config[1] = 0x00100003U;
  Config[3] = 0x00800010;
  Config[4] = 0x73000000;
  Config[5] = 0xc;
  Config[6] = 0x60;
  Config[7] = 0xc;
  Config[8] = 0x68;
  Config[9] = 0x5001;
  Config[12] = 0x74000000;
  Config[13] = 0x40;
  Config[16] = 0x00034801;
  Config[18] = 0x00020010;
  Config[19] = 0x10000000;
  Fpcie = 0x48;
  memset(&Pci, 0, sizeof(Pci));
  Pci.Pci.Read = FRead;
  Pci.Pci.Write = FWrite;
  Pci.Attributes = Attr;
  Bs.HandleProtocol = FHandle;
  Bs.LocateHandleBuffer = FHandles;
  Bs.ProtocolsPerHandle = FProtocols;
  Bs.OpenProtocolInformation = FOpens;
  Bs.AllocatePool = Pool;
  Bs.OpenProtocol = ExclusiveOpen;
  Bs.CloseProtocol = ExclusiveClose;
  Bs.Stall = Stall;
  Context.log = FLog;
  EFI_HANDLE actors[] = {VENDOR, BRIDGE, H(15), MEMBER, RUNTIME};
  memset(Fb, 0, sizeof(Fb));
  memset(Fi, 0, sizeof(Fi));
  for (unsigned i = 0; i < 5; i++) {
    Fb[i].DriverBindingHandle = actors[i];
    Fb[i].ImageHandle = H(200 + i);
    Fb[i].Supported = FSupported;
    Fb[i].Start = FStart;
    Fb[i].Stop = FStop;
    Fi[i].ImageBase = &Fi[i];
    Fi[i].ImageSize = 4096;
    Fi[i].ImageCodeType = i == 4 ? EfiRuntimeServicesCode : EfiBootServicesCode;
  }
}
static VOID FPrepare(GF_PLAN *p) {
  assert(GfPrepare(&Context, p) == EFI_SUCCESS && !Allocations && !FullStops);
}
#ifndef GC_PATCH_FIXTURE
int main(void) {
  GF_PLAN *p = malloc(sizeof(*p));
  assert(p);
  GC_RESULT r;
  FReset();
  FPrepare(p);
  assert(GfRun(&Context, p, &r) == EFI_SUCCESS && r.restored && Flrs == 1 && QuietWaits == 1 &&
         CloseCount == 1 && !Exclusive && !Allocations);
  FReset();
  Recreate = TRUE;
  FPrepare(p);
  assert(GfRun(&Context, p, &r) == EFI_SUCCESS && r.restored && NewHandles);
  FReset();
  Config[19] = 0;
  assert(GfPrepare(&Context, p) == EFI_UNSUPPORTED && !FullStops && !Flrs);
  FReset();
  Config[1] |= 4;
  assert(GfPrepare(&Context, p) == EFI_ACCESS_DENIED && !FullStops);
  FReset();
  Config[16] = (Config[16] & ~0xff00U) | 0x4000;
  assert(GfPrepare(&Context, p) == EFI_COMPROMISED_DATA && !FullStops);
  FReset();
  FPrepare(p);
  Config[1] |= 4;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && !FullStops && !r.touched);
  FReset();
  FPrepare(p);
  FailStopRole = 4;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && !Flrs && !r.restored);
  FReset();
  FPrepare(p);
  ChangeRuntimeInput = TRUE;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && !Flrs && VendorOn);
  FReset();
  FPrepare(p);
  Pending = TRUE;
  assert(GfRun(&Context, p, &r) == EFI_TIMEOUT && !Flrs && CloseCount == 1);
  FReset();
  FPrepare(p);
  FailResetWrite = TRUE;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && Flrs == 1 && QuietWaits == 1 && CloseCount == 1 &&
         !FullStarts);
  FReset();
  FPrepare(p);
  IdentityTimeout = TRUE;
  assert(GfRun(&Context, p, &r) == EFI_TIMEOUT && Flrs == 1 && !FullStarts && CloseCount == 1);
  FReset();
  FPrepare(p);
  FailRestore = TRUE;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && Flrs == 1 && !FullStarts && CloseCount == 1);
  FReset();
  FPrepare(p);
  FailStartRole = 1;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && Flrs == 1 && !r.restored);
  FReset();
  FPrepare(p);
  FailStartRole = 5;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && Flrs == 1 && !r.restored);
  FReset();
  FPrepare(p);
  FailLogRun = TRUE;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && !Flrs);
  FReset();
  FPrepare(p);
  ExposeGop = TRUE;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && !Flrs && !r.restored);
  assert(strstr(FullLog, "role=physical_gop") && strstr(FullLog, "rejected=1") &&
         strstr(FullLog, "role=sibling_edid") && strstr(FullLog, "FULL_GOP_ENUM"));
  FReset();
  FPrepare(p);
  ResidualOwner = TRUE;
  ExposeGop = TRUE;
  assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && !Flrs && r.claim == EFI_NOT_STARTED);
  assert(strstr(FullLog, "FULL_ROOT_OPEN agent=0x63") && strstr(FullLog, "owners=1") &&
         strstr(FullLog, "role=sibling_edid"));
  FReset();
  FPrepare(p);
  PathlessGop = TRUE;
  assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && !Flrs &&
         strstr(FullLog, "FULL_REMAINING_GOP") && strstr(FullLog, "rejected=1"));
  FReset();
  FPrepare(p);
  PathlessGop = TRUE;
  VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && !Flrs &&
         strstr(FullLog, "known_intermediate=0"));
  FReset();
  FPrepare(p);
  FailIsolationLog = TRUE;
  assert(GfRun(&Context, p, &r) == EFI_DEVICE_ERROR && !Flrs && r.claim == EFI_NOT_STARTED);
  FReset();
  FPrepare(p);
  UnexpectedMaster = TRUE;
  assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && Flrs == 1 && !(Config[1] & 4) &&
         !FullStarts);
  FReset();
  FPrepare(p);
  FailClose = TRUE;
  assert(EFI_ERROR(GfRun(&Context, p, &r)) && Flrs == 1 && Exclusive && !FullStarts &&
         CloseCount == 1);
  FReset();
  FPrepare(p);
  ResidualVirtual = TRUE;
  VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, p, &r) == EFI_SUCCESS && r.restored && Flrs == 1 &&
         strstr(FullLog, "allowed=1"));
  FReset();
  Recreate = TRUE;
  FPrepare(p);
  ResidualVirtual = TRUE;
  VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, p, &r) == EFI_SUCCESS && r.restored && Flrs == 1 && NewHandles);
  FReset();
  FPrepare(p);
  ResidualVirtual = TRUE;
  VirtualMode.FrameBufferBase = 1;
  VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && !Flrs);
  FReset();
  FPrepare(p);
  ResidualVirtual = TRUE;
  VirtualMode.FrameBufferBase = 0;
  VirtualMode.FrameBufferSize = 1;
  assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && !Flrs);
  UINT32 attrs[] = {EFI_OPEN_PROTOCOL_BY_DRIVER, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,
                    EFI_OPEN_PROTOCOL_EXCLUSIVE};
  for (unsigned i = 0; i < 3; i++) {
    FReset();
    FPrepare(p);
    ResidualVirtual = TRUE;
    VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
    VirtualOwnerAttr = attrs[i];
    assert(GfRun(&Context, p, &r) == EFI_ACCESS_DENIED && !Flrs && r.claim == EFI_NOT_STARTED &&
           strstr(FullLog, "allowed=0"));
  }
  FReset();
  FPrepare(p);
  ResidualVirtual = HideVirtual = TRUE;
  VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, p, &r) == EFI_COMPROMISED_DATA && !Flrs);
  FReset();
  FPrepare(p);
  ResidualVirtual = FailVirtualLog = TRUE;
  VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, p, &r) == EFI_DEVICE_ERROR && !Flrs);
  assert(!Allocations);
  free(p);
  puts("PASS 31 full GOP/FLR/rejoin scenarios; one reset, 100ms isolation and shared input guards");
  return 0;
}
#endif
