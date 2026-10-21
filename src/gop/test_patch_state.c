#include "patch.h"
#include "visual.h"
#include "../unlock/port.h"
#include "../unlock/app_io.h"
#include "../unlock/vendor/gfw_eval.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UINT32 Identity, Ss0, Ss1, Chip;
static UINT16 Command;
static BOOLEAN ReadError, ReportOk, PatchLogOk;
static unsigned Reads, Reports;
VOID GuLog(const char *format, ...) { (void)format; }
VOID GuLogText(const char *text) { (void)text; }
VOID GuConsole(const char *text) {
  (void)text;
  assert(0);
}
BOOLEAN GuLogReady(VOID) { return PatchLogOk; }
VOID GuIoSetQuiet(BOOLEAN quiet) { (void)quiet; }
EFI_STATUS GuIoOpen(EFI_HANDLE h, EFI_SYSTEM_TABLE *t) {
  (void)h;
  (void)t;
  return EFI_UNSUPPORTED;
}
EFI_STATUS GuIoOpenRun(EFI_HANDLE h, EFI_SYSTEM_TABLE *t, UINTN slot) {
  (void)slot;
  return GuIoOpen(h, t);
}
EFI_STATUS GuIoClose(VOID) { return EFI_SUCCESS; }
EFI_STATUS GuLoadAssets(VOID) { return EFI_UNSUPPORTED; }
VOID GuReleaseAssets(VOID) {}
EFI_STATUS GvClockInit(GC_CONTEXT *c) {
  (void)c;
  return EFI_SUCCESS;
}
BOOLEAN GvMark(GC_CONTEXT *c, const char *phase) {
  (void)c;
  (void)phase;
  return TRUE;
}
static BOOLEAN Report(const char *format, ...) {
  assert(strstr(format, "PATCH_STATE") || strstr(format, "SELECTOR_PREFLIGHT"));
  Reports++;
  return ReportOk;
}
static EFI_STATUS EFIAPI ConfigRead(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT32 o,
                                    UINTN n, VOID *v) {
  (void)p;
  assert(n == 1);
  if (o == 0) {
    assert(w == EfiPciIoWidthUint32);
    *(UINT32 *)v = Identity;
  } else {
    assert(o == 4 && w == EfiPciIoWidthUint16);
    *(UINT16 *)v = Command;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI MemRead(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT8 b,
                                 UINT64 o, UINTN n, VOID *v) {
  (void)p;
  assert(w == EfiPciIoWidthUint32 && b == 0 && n == 1);
  Reads++;
  if (ReadError) return EFI_DEVICE_ERROR;
  if (o == 0)
    *(UINT32 *)v = Chip;
  else if (o == GU_FEAT_SS0_OFFSET)
    *(UINT32 *)v = Ss0;
  else if (o == GU_FEAT_SS1_OFFSET)
    *(UINT32 *)v = Ss1;
  else {
    assert(o == GU_FEAT_PLM_OFFSET);
    *(UINT32 *)v = 0xffffff8f;
  }
  return EFI_SUCCESS;
}
static EFI_PCI_IO_PROTOCOL Pci;
static GC_CONTEXT Context;
static void Reset(void) {
  GuInit(NULL, &Pci);
  Gu.bar_size = 0x1000000;
  Identity = GU_BOARD_ID;
  Chip = GU_BOARD_CHIP;
  Command = 3;
  Ss0 = 0x88888888;
  Ss1 = 8;
  ReadError = FALSE;
  ReportOk = PatchLogOk = TRUE;
  Reads = Reports = 0;
}
int main(void) {
  Pci.Pci.Read = ConfigRead;
  Pci.Mem.Read = MemRead;
  Context.pci = &Pci;
  Context.log = Report;
  Reset();
  assert(GxObserve(&Context, "before-gop") == EFI_SUCCESS && Reads == 3 && Reports == 1);
  Reset();
  assert(GxObserve(&Context, "after-gop") == EFI_SUCCESS && !Gu.armed && !Gu.owned);
  Reset();
  Ss0 = 0x20740011;
  assert(GxObserve(&Context, "after-gop") == EFI_COMPROMISED_DATA && !Gu.fault);
  Reset();
  Ss1 = 6;
  assert(GxObserve(&Context, "after-gop") == EFI_COMPROMISED_DATA);
  Reset();
  ReadError = TRUE;
  assert(GxObserve(&Context, "after-gop") == EFI_DEVICE_ERROR && Gu.fault);
  Reset();
  Command |= 4;
  assert(GxObserve(&Context, "after-gop") == EFI_DEVICE_ERROR && !Reads);
  Reset();
  Command = 1;
  assert(GxObserve(&Context, "after-gop") == EFI_DEVICE_ERROR && !Reads);
  Reset();
  Identity ^= 1;
  assert(GxObserve(&Context, "after-gop") == EFI_DEVICE_ERROR && !Reads);
  Reset();
  ReportOk = FALSE;
  assert(GxObserve(&Context, "after-gop") == EFI_DEVICE_ERROR);
  Reset();
  PatchLogOk = FALSE;
  assert(GxObserve(&Context, "after-gop") == EFI_DEVICE_ERROR);
#ifdef GU_SSD_EXPERIMENT
  BOOLEAN skip;
  Reset();
  Ss1 = 0;
  assert(GxInitialSelectors(&Context, &skip) == EFI_SUCCESS && skip);
  Reset();
  Ss0 = 0;
  Ss1 = 0;
  assert(GxInitialSelectors(&Context, &skip) == EFI_SUCCESS && !skip);
  Reset();
  Ss0 = 0x12345678;
  assert(GxInitialSelectors(&Context, &skip) == EFI_SUCCESS && !skip);
  Reset();
  ReadError = TRUE;
  assert(GxInitialSelectors(&Context, &skip) == EFI_DEVICE_ERROR && !skip);
  Reset();
  Chip ^= 1;
  assert(GxInitialSelectors(&Context, &skip) == EFI_UNSUPPORTED && !skip);
  Reset();
  ReportOk = FALSE;
  assert(GxInitialSelectors(&Context, &skip) == EFI_DEVICE_ERROR);
  puts("PASS 6 initial selector decisions: SS0-only skip, unexpected values attempt, read/chip/log "
       "faults stop");
#endif
  puts("PASS 10 actual selector observation scenarios; no write services installed");
  return 0;
}
