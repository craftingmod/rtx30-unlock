#include "leaf.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextIn.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define H(n) ((EFI_HANDLE)(UINTN)(n))
#define ROOT H(1)
#define PHYS H(2)
#define TEXT H(3)
#define SHARED H(4)
#define EDID H(5)
#define VENDOR H(11)
#define BRIDGE H(12)
#define MEMBER H(13)
#define RUNTIME H(14)
static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID, PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,
                InputGuid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,
                BindingGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID,
                LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID, EdidGuid = {0x1c0c34f6, 0, 0, {0}};
static EFI_SYSTEM_TABLE Table;
static EFI_BOOT_SERVICES Bs;
static GC_CONTEXT Context;
static EFI_PCI_IO_PROTOCOL Pci;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL LocalText, SharedText;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static EFI_DRIVER_BINDING_PROTOCOL Binding;
static EFI_LOADED_IMAGE_PROTOCOL Image;
static EFI_DEVICE_PATH_PROTOCOL Path;
static unsigned Bits, StopBits, Stops, Supports, Starts, Allocations;
static EFI_STATUS StopStatus, SupportedStatus, StartStatus;
static BOOLEAN Foreign, MultiGop, WrongChild, WrongOwner, GuardChanged, ChangeGuardOnStop,
    ChangeInputOnStop;
static BOOLEAN ChangeBindingOnStop, FailLogBefore, FailLogAfter, Reverse, StartPartial;
static BOOLEAN Eq(EFI_GUID *a, EFI_GUID *b) { return !memcmp(a, b, sizeof(*a)); }
static VOID *Alloc(UINTN n) {
  VOID *p = calloc(1, n);
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
  return !FailLogBefore && !(FailLogAfter && Stops);
}
static EFI_STATUS EFIAPI Output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *t, CHAR16 *s) {
  (void)t;
  (void)s;
  assert(!"leaf core must not use a console");
  return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI Stop(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE controller, UINTN n,
                              EFI_HANDLE *children) {
  assert(b == &Binding && controller == TEXT && n == 1 && children && children[0] == SHARED &&
         !Stops);
  Stops++;
  Bits = StopBits;
  if (ChangeGuardOnStop) GuardChanged = TRUE;
  if (ChangeInputOnStop) Table.ConIn = NULL;
  if (ChangeBindingOnStop) Binding.Version++;
  return StopStatus;
}
static EFI_STATUS EFIAPI Supported(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE controller,
                                   EFI_DEVICE_PATH_PROTOCOL *path) {
  assert(b == &Binding && controller == TEXT && !path && Stops == 1 && !Supports);
  Supports++;
  return SupportedStatus;
}
static EFI_STATUS EFIAPI Start(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE controller,
                               EFI_DEVICE_PATH_PROTOCOL *path) {
  assert(b == &Binding && controller == TEXT && !path && Supports == 1 &&
         SupportedStatus == EFI_SUCCESS && !Starts);
  Starts++;
  if (!EFI_ERROR(StartStatus)) Bits = StartPartial ? 1 : 3;
  return StartStatus;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *g, VOID **out) {
  *out = NULL;
  if (h == ROOT && Eq(g, &PciGuid))
    *out = &Pci;
  else if (h == PHYS && Eq(g, &PathGuid))
    *out = &Path;
  else if (h == PHYS && Eq(g, &GopGuid))
    *out = &Gop;
  else if (h == TEXT && Eq(g, &TextGuid))
    *out = &LocalText;
  else if (h == SHARED && Eq(g, &TextGuid))
    *out = &SharedText;
  else if (h == SHARED && Eq(g, &InputGuid))
    *out = &Input;
  else if (h == MEMBER && Eq(g, &BindingGuid))
    *out = &Binding;
  else if (h == H(21) && Eq(g, &LoadedGuid))
    *out = &Image;
  return *out ? EFI_SUCCESS : EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Locate(EFI_GUID *g, EFI_DEVICE_PATH_PROTOCOL **path, EFI_HANDLE *h) {
  assert(Eq(g, &PciGuid) && *path == &Path);
  *h = Foreign ? H(99) : ROOT;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handles(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *g, VOID *key, UINTN *n,
                                 EFI_HANDLE **out) {
  (void)key;
  assert(type == ByProtocol && Eq(g, &GopGuid));
  *n = MultiGop ? 2 : 1;
  *out = Alloc(*n * sizeof(**out));
  for (UINTN i = 0; i < *n; i++)
    (*out)[i] = PHYS;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h, EFI_GUID ***out, UINTN *n) {
  *n = 2;
  *out = Alloc(*n * sizeof(**out));
  if (h == ROOT) {
    (*out)[0] = &PciGuid;
    (*out)[1] = &PathGuid;
  } else if (h == PHYS) {
    (*out)[0] = &GopGuid;
    (*out)[1] = &EdidGuid;
  } else if (h == TEXT) {
    (*out)[0] = &TextGuid;
    (*out)[1] = &GopGuid;
  } else if (h == SHARED) {
    (*out)[0] = &TextGuid;
    (*out)[1] = &InputGuid;
  } else {
    assert(h == EDID);
    (*out)[0] = &EdidGuid;
    (*out)[1] = &PathGuid;
  }
  return EFI_SUCCESS;
}
static VOID Add(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e, UINTN *n, EFI_HANDLE agent,
                EFI_HANDLE controller, UINT32 attr) {
  e[*n].AgentHandle = agent;
  e[*n].ControllerHandle = controller;
  e[*n].Attributes = attr;
  e[*n].OpenCount = 1;
  (*n)++;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h, EFI_GUID *g, EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,
                               UINTN *n) {
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY entries[4] = {0};
  *n = 0;
  if (h == ROOT && Eq(g, &PciGuid))
    Add(entries, n, VENDOR, ROOT, EFI_OPEN_PROTOCOL_BY_DRIVER);
  else if (h == ROOT && Eq(g, &PathGuid)) {
    Add(entries, n, VENDOR, PHYS, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
    Add(entries, n, VENDOR, EDID, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  } else if (h == PHYS && Eq(g, &GopGuid)) {
    Add(entries, n, BRIDGE, PHYS, EFI_OPEN_PROTOCOL_BY_DRIVER);
    Add(entries, n, BRIDGE, TEXT, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  } else if (h == TEXT && Eq(g, &TextGuid)) {
    if (Bits & 1) Add(entries, n, MEMBER, TEXT, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (Bits & 2)
      Add(entries, n, WrongOwner ? VENDOR : MEMBER, WrongChild ? EDID : SHARED,
          EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  } else if ((h == PHYS || h == EDID) && Eq(g, &EdidGuid))
    Add(entries, n, RUNTIME, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
  else if (h == SHARED && Eq(g, &InputGuid))
    Add(entries, n, GuardChanged ? VENDOR : RUNTIME, h, EFI_OPEN_PROTOCOL_BY_DRIVER);
  else if (h == TEXT && Eq(g, &GopGuid))
    Add(entries, n, H(15), h, EFI_OPEN_PROTOCOL_BY_DRIVER);
  *out = NULL;
  if (*n) {
    *out = Alloc(*n * sizeof(**out));
    for (UINTN i = 0; i < *n; i++)
      (*out)[i] = entries[Reverse && Stops ? *n - i - 1 : i];
  }
  return EFI_SUCCESS;
}
static VOID Reset(VOID) {
  assert(!Allocations);
  memset(&Table, 0, sizeof(Table));
  memset(&Bs, 0, sizeof(Bs));
  memset(&Context, 0, sizeof(Context));
  memset(&Gop, 0, sizeof(Gop));
  memset(&Mode, 0, sizeof(Mode));
  memset(&Info, 0, sizeof(Info));
  memset(&Binding, 0, sizeof(Binding));
  memset(&Image, 0, sizeof(Image));
  Bits = 3;
  StopBits = 0;
  Stops = Supports = Starts = 0;
  StopStatus = SupportedStatus = StartStatus = EFI_SUCCESS;
  Foreign = MultiGop = WrongChild = WrongOwner = GuardChanged = ChangeGuardOnStop =
      ChangeInputOnStop = FALSE;
  ChangeBindingOnStop = FailLogBefore = FailLogAfter = Reverse = StartPartial = FALSE;
  Bs.HandleProtocol = Handle;
  Bs.LocateDevicePath = Locate;
  Bs.LocateHandleBuffer = Handles;
  Bs.ProtocolsPerHandle = Protocols;
  Bs.OpenProtocolInformation = Opens;
  Bs.FreePool = Free;
  Table.BootServices = &Bs;
  Table.ConsoleOutHandle = Table.ConsoleInHandle = Table.StandardErrorHandle = SHARED;
  Table.ConIn = &Input;
  Table.ConOut = Table.StdErr = &SharedText;
  LocalText.OutputString = SharedText.OutputString = Output;
  Info.HorizontalResolution = 1024;
  Info.VerticalResolution = 768;
  Info.PixelsPerScanLine = 1024;
  Info.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Mode.Info = &Info;
  Mode.SizeOfInfo = sizeof(Info);
  Mode.FrameBufferBase = 0x6000000000ULL;
  Mode.FrameBufferSize = 1024 * 768 * 4;
  Gop.Mode = &Mode;
  Binding.DriverBindingHandle = MEMBER;
  Binding.ImageHandle = H(21);
  Binding.Supported = Supported;
  Binding.Start = Start;
  Binding.Stop = Stop;
  Image.ImageBase = &Image;
  Image.ImageSize = 27200;
  Image.ImageCodeType = EfiBootServicesCode;
  Context.table = &Table;
  Context.target = ROOT;
  Context.image = H(30);
  Context.log = Log;
  Context.pci = &Pci;
  /* Generic Connect/DisconnectController, exclusive opens and PCI writes are deliberately absent. */
}
static VOID Prepare(GL_PLAN *p) {
  assert(GcLeafPrepare(&Context, p) == EFI_SUCCESS && p->guard.member == 3 &&
         p->handle_count == 5 && !Allocations);
}
int main(void) {
  GL_PLAN p;
  GC_RESULT r;
  Reset();
  Prepare(&p);
  assert(GcLeafRun(&Context, &p, &r) == EFI_SUCCESS && r.restored && Stops == 1 && Starts == 1);
  Reset();
  Prepare(&p);
  Reverse = TRUE;
  assert(GcLeafRun(&Context, &p, &r) == EFI_SUCCESS && r.restored);
  Reset();
  Foreign = TRUE;
  assert(EFI_ERROR(GcLeafPrepare(&Context, &p)) && !Stops);
  Reset();
  MultiGop = TRUE;
  assert(EFI_ERROR(GcLeafPrepare(&Context, &p)) && !Stops);
  Reset();
  WrongChild = TRUE;
  assert(EFI_ERROR(GcLeafPrepare(&Context, &p)) && !Stops);
  Reset();
  WrongOwner = TRUE;
  assert(EFI_ERROR(GcLeafPrepare(&Context, &p)) && !Stops);
  Reset();
  Image.ImageCodeType = EfiRuntimeServicesCode;
  assert(EFI_ERROR(GcLeafPrepare(&Context, &p)) && !Stops);
  Reset();
  Prepare(&p);
  Binding.Version++;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !Stops);
  Reset();
  Prepare(&p);
  GuardChanged = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !Stops);
  Reset();
  Prepare(&p);
  FailLogBefore = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !Stops);
  Reset();
  Prepare(&p);
  StopStatus = EFI_DEVICE_ERROR;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && r.restored && Starts == 1);
  Reset();
  Prepare(&p);
  StopBits = 3;
  StopStatus = EFI_DEVICE_ERROR;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && r.restored && !Starts);
  Reset();
  Prepare(&p);
  StopBits = 1;
  SupportedStatus = EFI_ALREADY_STARTED;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored && !Starts && Supports == 1);
  Reset();
  Prepare(&p);
  SupportedStatus = EFI_UNSUPPORTED;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored && !Starts);
  Reset();
  Prepare(&p);
  StartStatus = EFI_DEVICE_ERROR;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored && Starts == 1);
  Reset();
  Prepare(&p);
  StartPartial = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored);
  Reset();
  Prepare(&p);
  ChangeGuardOnStop = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored && !Starts);
  Reset();
  Prepare(&p);
  ChangeInputOnStop = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored && !Starts);
  Reset();
  Prepare(&p);
  ChangeBindingOnStop = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && !r.restored && !Starts);
  Reset();
  Prepare(&p);
  FailLogAfter = TRUE;
  assert(EFI_ERROR(GcLeafRun(&Context, &p, &r)) && r.restored && Starts == 1);
  assert(!Allocations);
  puts("PASS 20 single text-member detach/restore scenarios; shared input/GPU/EDID guarded");
  return 0;
}
