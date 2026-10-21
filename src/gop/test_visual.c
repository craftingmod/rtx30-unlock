/* Real visual/timing code, with no GPU or firmware services. */
#include "visual.c"
#include "../unlock/format.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
static EFI_BOOT_SERVICES Services;
static EFI_RUNTIME_SERVICES Runtime;
static EFI_SYSTEM_TABLE Table;
static GC_CONTEXT Context;
static GL_PLAN Old, Fresh;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
static EFI_TIME Now;
static UINT32 Framebuffer[640 * 480];
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Alternate;
static BOOLEAN NoAlternate, BadReadback, FailAlternateRead, FailReadbackRead, FailReadbackLog;
static unsigned FailSetCall;
static unsigned Sets, Blts, Queries, Frees, Observations;
static EFI_STATUS QueryStatus, SetStatus, BltStatus, ClockStatus;
static BOOLEAN BadQuery, BadMode, FailModeRead, FailBltRead, FailIntent;
static char LogData[8192];
static size_t LogBytes;
static BOOLEAN Log(const char *fmt, ...) {
  if (FailReadbackLog && strstr(fmt, "VISUAL_READBACK_INTENT")) return FALSE;
  if (FailIntent && strstr(fmt, "VISUAL_SET_MODE_INTENT")) return FALSE;
  va_list args;
  va_start(args, fmt);
  size_t n = GuFormatV(LogData + LogBytes, sizeof(LogData) - LogBytes, fmt, args);
  va_end(args);
  assert(n < sizeof(LogData) - LogBytes);
  LogBytes += n;
  return TRUE;
}
EFI_STATUS GxObserve(GC_CONTEXT *c, const char *phase) {
  assert(c == &Context);
  Observations++;
  if (!strcmp(phase, "after-alternate-mode")) {
    assert(Sets == 1 && !Blts);
    return FailAlternateRead ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
  }
  if (!strcmp(phase, "after-set-mode")) {
    assert(Sets == 2 && !Blts);
    return FailModeRead ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
  }
  if (!strcmp(phase, "after-blt")) {
    assert(Sets == 2 && Blts == 1);
    return FailBltRead ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
  }
  assert(!strcmp(phase, "after-readback") && Sets == 2 && Blts == 1);
  return FailReadbackRead ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *g, VOID **p) {
  EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  assert(h == Fresh.physical && !memcmp(g, &guid, sizeof(guid)));
  *p = &Gop;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FreePool(VOID *p) {
  Frees++;
  free(p);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Query(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode, UINTN *bytes,
                               EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info) {
  assert(g == &Gop && (mode == 2 || mode < 2));
  Queries++;
  *bytes = sizeof(Info);
  *info = malloc(sizeof(Info));
  assert(*info);
  **info = mode == 2 || NoAlternate ? Info : Alternate;
  if (BadQuery && mode == 2) (*info)->HorizontalResolution++;
  return QueryStatus;
}
static EFI_STATUS EFIAPI Set(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode) {
  assert(g == &Gop && mode == (Sets ? 2 : 0) && Sets < 2);
  Sets++;
  Mode.Mode = BadMode ? 1 : mode;
  Info = mode == 2 ? Old.info : Alternate;
  Mode.FrameBufferSize = sizeof(Framebuffer);
  return Sets == FailSetCall ? SetStatus : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p,
                             EFI_GRAPHICS_OUTPUT_BLT_OPERATION op, UINTN sx, UINTN sy, UINTN dx,
                             UINTN dy, UINTN w, UINTN h, UINTN delta) {
  assert(g == &Gop && Sets == 2 && !Blts && p->Green == 255 && !p->Red && !p->Blue &&
         op == EfiBltVideoFill);
  assert(!sx && !sy && !delta && dx == 544 && dy == 384 && w == 96 && h == 96);
  Blts++;
  for (UINTN y = dy; y < dy + h; y++)
    for (UINTN x = dx; x < dx + w; x++)
      Framebuffer[y * 640 + x] = 0xab00ff00;
  if (BadReadback) Framebuffer[479 * 640 + 639] = 0x00ff0000;
  return BltStatus;
}

static EFI_STATUS EFIAPI Time(EFI_TIME *t, EFI_TIME_CAPABILITIES *caps) {
  assert(!caps);
  *t = Now;
  return ClockStatus;
}
static void Setup(void) {
  memset(&Services, 0, sizeof(Services));
  memset(&Runtime, 0, sizeof(Runtime));
  memset(&Table, 0, sizeof(Table));
  Services.HandleProtocol = Handle;
  Services.FreePool = FreePool;
  Runtime.GetTime = Time;
  Table.BootServices = &Services;
  Table.RuntimeServices = &Runtime;
  memset(&Context, 0, sizeof(Context));
  Context.table = &Table;
  Context.log = Log;
  memset(&Old, 0, sizeof(Old));
  memset(&Fresh, 0, sizeof(Fresh));
  memset(&Info, 0, sizeof(Info));
  memset(&Mode, 0, sizeof(Mode));
  Info.HorizontalResolution = 640;
  Info.VerticalResolution = 480;
  Info.PixelsPerScanLine = 640;
  Info.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Old.mode = 2;
  Old.info = Info;
  Fresh.physical = (EFI_HANDLE)(UINTN)1;
  Fresh.gop = &Gop;
  Mode.Mode = 2;
  Mode.MaxMode = 3;
  Mode.Info = &Info;
  Mode.SizeOfInfo = sizeof(Info);
  Mode.FrameBufferBase = (UINTN)Framebuffer;
  Mode.FrameBufferSize = 640 * 480 * 4;
  Alternate = Info;
  Alternate.HorizontalResolution = 320;
  Alternate.VerticalResolution = 240;
  Gop.Mode = &Mode;
  Gop.QueryMode = Query;
  Gop.SetMode = Set;
  Gop.Blt = Blt;
  memset(&Now, 0, sizeof(Now));
  Now.Year = 2026;
  Now.Month = 10;
  Now.Day = 20;
  Now.Hour = 10;
  Sets = Blts = Queries = Frees = Observations = 0;
  LogBytes = 0;
  memset(LogData, 0, sizeof(LogData));
  QueryStatus = SetStatus = BltStatus = ClockStatus = EFI_SUCCESS;
  BadQuery = BadMode = FailModeRead = FailBltRead = FailIntent = FALSE;
  NoAlternate = BadReadback = FailAlternateRead = FailReadbackRead = FailReadbackLog = FALSE;
  FailSetCall = 1;
  memset(Framebuffer, 0, sizeof(Framebuffer));
  assert(GvClockInit(&Context) == EFI_SUCCESS);
}
int main(void) {
  Setup();
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_SUCCESS && Sets == 2 && Blts == 1 &&
         Observations == 4 && Frees == 2 && strstr(LogData, "mismatches=0"));
  Setup();
  BadQuery = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && !Sets && Frees == 1);
  Setup();
  QueryStatus = EFI_DEVICE_ERROR;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_DEVICE_ERROR && !Sets && Frees == 1);
  Setup();
  SetStatus = EFI_DEVICE_ERROR;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_DEVICE_ERROR && Sets == 1 && !Blts &&
         Observations == 1);
  Setup();
  BadMode = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && Sets == 1 && !Blts);
  Setup();
  FailModeRead = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && !Blts);
  Setup();
  BltStatus = EFI_DEVICE_ERROR;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_DEVICE_ERROR && Sets == 2 && Blts == 1 &&
         Observations == 3);
  Setup();
  FailBltRead = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && Blts == 1);
  Setup();
  FailIntent = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_DEVICE_ERROR && !Sets && !Blts);
  Setup();
  FailSetCall = 2;
  SetStatus = EFI_DEVICE_ERROR;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_DEVICE_ERROR && Sets == 2 && Observations == 2 &&
         !Blts);
  Setup();
  FailAlternateRead = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && Sets == 1 && !Blts);
  Setup();
  NoAlternate = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_UNSUPPORTED && !Sets && Frees == 3);
  Setup();
  Mode.MaxMode = 129;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_UNSUPPORTED && !Queries && !Sets);
  Setup();
  BadReadback = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && Observations == 4 &&
         strstr(LogData, "mismatches=1"));
  Setup();
  FailReadbackRead = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_COMPROMISED_DATA && Observations == 4);
  Setup();
  FailReadbackLog = TRUE;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_DEVICE_ERROR && Observations == 3);
  Setup();
  Old.info.PixelFormat = Info.PixelFormat = Alternate.PixelFormat =
      PixelRedGreenBlueReserved8BitPerColor;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_SUCCESS);
  Setup();
  Gop.SetMode = NULL;
  assert(GvDisplay(&Context, &Old, &Fresh) == EFI_UNSUPPORTED && !Sets);
  Setup();
  Now.Second = 7;
  assert(GvMark(&Context, "test") && strstr(LogData, "elapsed_ms=7000 since_last_ms=7000"));
  Now.Second = 3;
  assert(GvMark(&Context, "backward") && strstr(LogData, "phase=backward clock_valid=0"));
  Setup();
  ClockStatus = EFI_DEVICE_ERROR;
  assert(GvClockInit(&Context) == EFI_DEVICE_ERROR);
  Setup();
  Now.Month = 2;
  Now.Day = 29;
  assert(GvClockInit(&Context) == EFI_COMPROMISED_DATA);
  Setup();
  Now.Year = 2024;
  Now.Month = 2;
  Now.Day = 28;
  Now.Hour = 23;
  Now.Minute = 59;
  Now.Second = 59;
  assert(GvClockInit(&Context) == EFI_SUCCESS);
  Now.Day = 29;
  Now.Hour = Now.Minute = Now.Second = 0;
  assert(GvMark(&Context, "leap") && strstr(LogData, "elapsed_ms=1000 since_last_ms=1000"));
  puts("PASS 22 actual display/RTC scenarios; alternate/restore, framebuffer readback, selector "
       "guards and calendar rollover");
  return 0;
}
