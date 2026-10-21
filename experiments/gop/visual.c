#include "visual.h"
#include "patch.h"
#include <string.h>
static UINT64 Started, Previous;
static BOOLEAN ClockReady;
static BOOLEAN Leap(UINTN y) { return !(y % 4) && (y % 100 || !(y % 400)); }
static BOOLEAN Milliseconds(const EFI_TIME *t, UINT64 *ms) {
  static const UINT8 months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (t->Year < 1900 || t->Year > 9999 || !t->Month || t->Month > 12 || !t->Day ||
      t->Day > months[t->Month - 1] + (t->Month == 2 && Leap(t->Year)) || t->Hour > 23 ||
      t->Minute > 59 || t->Second > 59 || t->Nanosecond >= 1000000000)
    return FALSE;
  UINT64 days = 0;
  for (UINTN y = 1900; y < t->Year; y++)
    days += 365 + Leap(y);
  for (UINTN m = 1; m < t->Month; m++)
    days += months[m - 1] + (m == 2 && Leap(t->Year));
  days += t->Day - 1;
  *ms =
      (((days * 24 + t->Hour) * 60 + t->Minute) * 60 + t->Second) * 1000 + t->Nanosecond / 1000000;
  return TRUE;
}
static EFI_STATUS ReadClock(GC_CONTEXT *c, EFI_TIME *t, UINT64 *ms) {
  memset(t, 0, sizeof(*t));
  if (!c->table->RuntimeServices || !c->table->RuntimeServices->GetTime) return EFI_UNSUPPORTED;
  EFI_STATUS s = c->table->RuntimeServices->GetTime(t, NULL);
  return !EFI_ERROR(s) && !Milliseconds(t, ms) ? EFI_COMPROMISED_DATA : s;
}
EFI_STATUS GvClockInit(GC_CONTEXT *c) {
  EFI_TIME t;
  ClockReady = FALSE;
  EFI_STATUS s = ReadClock(c, &t, &Started);
  if (!c->log("TIME_INIT status=%#llx source=firmware-RTC timezone=%d\n", (unsigned long long)s,
              (int)t.TimeZone))
    return EFI_DEVICE_ERROR;
  if (EFI_ERROR(s)) return s;
  Previous = Started;
  ClockReady = TRUE;
  return EFI_SUCCESS;
}
BOOLEAN GvMark(GC_CONTEXT *c, const char *phase) {
  EFI_TIME t;
  UINT64 now = 0;
  EFI_STATUS s = ReadClock(c, &t, &now);
  BOOLEAN valid = ClockReady && !EFI_ERROR(s) && now >= Previous;
  UINT64 elapsed = valid ? now - Started : 0, delta = valid ? now - Previous : 0;
  if (valid) Previous = now;
  return c->log("TIME phase=%s clock_valid=%u rtc=%04u-%02u-%02uT%02u:%02u:%02u elapsed_ms=%llu "
                "since_last_ms=%llu status=%#llx\n",
                phase, valid, t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second,
                (unsigned long long)elapsed, (unsigned long long)delta, (unsigned long long)s);
}
static BOOLEAN ValidMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 number,
                         const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m = gop->Mode;
  return m && m->Mode == number && m->Info && m->SizeOfInfo >= sizeof(*info) &&
         !memcmp(m->Info, info, sizeof(*info)) && m->FrameBufferBase && !(m->FrameBufferBase & 3) &&
         m->FrameBufferSize && m->FrameBufferBase <= (UINT64)(UINTN)-1 - m->FrameBufferSize &&
         info->PixelFormat <= PixelBlueGreenRedReserved8BitPerColor && info->HorizontalResolution &&
         info->VerticalResolution && info->PixelsPerScanLine >= info->HorizontalResolution &&
         (UINT64)info->PixelsPerScanLine * info->VerticalResolution <= m->FrameBufferSize / 4;
}
static EFI_STATUS SetMode(GC_CONTEXT *c, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 number,
                          const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info, const char *phase) {
  if (!c->log("VISUAL_SET_MODE_INTENT phase=%s mode=%u\n", phase, number) ||
      !GvMark(c, "set-mode-begin"))
    return EFI_DEVICE_ERROR;
  EFI_STATUS s = gop->SetMode(gop, number);
  if (!GvMark(c, "set-mode-end") ||
      !c->log("VISUAL_SET_MODE_RESULT phase=%s mode=%u status=%#llx\n", phase, number,
              (unsigned long long)s))
    return EFI_DEVICE_ERROR;
  EFI_STATUS observed = GxObserve(c, phase);
  if (s != EFI_SUCCESS) return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
  if (EFI_ERROR(observed)) return observed;
  return ValidMode(gop, number, info) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}
EFI_STATUS GvDisplay(GC_CONTEXT *c, const GL_PLAN *original, const GL_PLAN *fresh) {
  EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  EFI_STATUS s = c->table->BootServices->HandleProtocol(fresh->physical, &guid, (VOID **)&gop);
  if (EFI_ERROR(s) || !gop || gop != fresh->gop || !gop->Mode || !gop->QueryMode || !gop->SetMode ||
      !gop->Blt || original->mode >= gop->Mode->MaxMode || gop->Mode->MaxMode > 128)
    return EFI_UNSUPPORTED;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
  UINTN bytes = 0;
  s = gop->QueryMode(gop, original->mode, &bytes, &info);
  BOOLEAN same = !EFI_ERROR(s) && info && bytes >= sizeof(*info) &&
                 !memcmp(info, &original->info, sizeof(*info));
  if (info && EFI_ERROR(c->table->BootServices->FreePool(info))) return EFI_DEVICE_ERROR;
  if (!c->log("VISUAL_QUERY status=%#llx mode=%u original_match=%u\n", (unsigned long long)s,
              original->mode, same))
    return EFI_DEVICE_ERROR;
  if (!same) return EFI_ERROR(s) ? s : EFI_COMPROMISED_DATA;
  UINT32 alternate = gop->Mode->MaxMode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION alternateInfo;
  for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
    if (i == original->mode || i == gop->Mode->Mode) continue;
    info = NULL;
    bytes = 0;
    s = gop->QueryMode(gop, i, &bytes, &info);
    BOOLEAN usable = s == EFI_SUCCESS && info && bytes >= sizeof(*info) &&
                     info->PixelFormat <= PixelBlueGreenRedReserved8BitPerColor &&
                     info->HorizontalResolution && info->VerticalResolution &&
                     info->PixelsPerScanLine >= info->HorizontalResolution &&
                     (info->HorizontalResolution != original->info.HorizontalResolution ||
                      info->VerticalResolution != original->info.VerticalResolution);
    if (usable) alternateInfo = *info;
    if (!c->log(
            "VISUAL_ALTERNATE_QUERY mode=%u status=%#llx bytes=%zu width=%u height=%u usable=%u\n",
            i, (unsigned long long)s, (size_t)bytes,
            info && bytes >= sizeof(*info) ? info->HorizontalResolution : 0,
            info && bytes >= sizeof(*info) ? info->VerticalResolution : 0, usable)) {
      if (info) c->table->BootServices->FreePool(info);
      return EFI_DEVICE_ERROR;
    }
    if (info && EFI_ERROR(c->table->BootServices->FreePool(info))) return EFI_DEVICE_ERROR;
    if (s != EFI_SUCCESS) return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
    if (usable) {
      alternate = i;
      break;
    }
  }
  if (!c->log("VISUAL_MODE_CYCLE original=%u current=%u alternate=%u found=%u\n", original->mode,
              gop->Mode->Mode, alternate, alternate < gop->Mode->MaxMode))
    return EFI_DEVICE_ERROR;
  if (alternate == gop->Mode->MaxMode) return EFI_UNSUPPORTED;
  s = SetMode(c, gop, alternate, &alternateInfo, "after-alternate-mode");
  if (EFI_ERROR(s)) return s;
  s = SetMode(c, gop, original->mode, &original->info, "after-set-mode");
  if (EFI_ERROR(s)) return s;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode = gop->Mode;
  UINTN width = mode->Info->HorizontalResolution, height = mode->Info->VerticalResolution;
  UINTN block = (width < height ? width : height) / 4;
  if (block > 96) block = 96;
  if (!block) return EFI_UNSUPPORTED;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL green = {.Green = 255};
  if (!c->log("VISUAL_BLT_INTENT color=green x=%zu y=%zu width=%zu height=%zu framebuffer=%#llx\n",
              (size_t)(width - block), (size_t)(height - block), (size_t)block, (size_t)block,
              (unsigned long long)mode->FrameBufferBase) ||
      !GvMark(c, "blt-begin"))
    return EFI_DEVICE_ERROR;
  s = gop->Blt(gop, &green, EfiBltVideoFill, 0, 0, width - block, height - block, block, block, 0);
  if (!GvMark(c, "blt-end") ||
      !c->log("VISUAL_BLT_RESULT status=%#llx visual_confirmation=pending\n",
              (unsigned long long)s))
    return EFI_DEVICE_ERROR;
  EFI_STATUS observed = GxObserve(c, "after-blt");
  if (s != EFI_SUCCESS) return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
  if (EFI_ERROR(observed)) return observed;
  if (!ValidMode(gop, original->mode, &original->info) || gop->Mode != mode)
    return EFI_COMPROMISED_DATA;
  if (!c->log("VISUAL_READBACK_INTENT source=direct-framebuffer pixels=%zu mask=00ffffff "
              "expected=0000ff00\n",
              (size_t)(block * block)) ||
      !GvMark(c, "readback-begin"))
    return EFI_DEVICE_ERROR;
  volatile UINT32 *fb = (volatile UINT32 *)(UINTN)mode->FrameBufferBase;
  UINTN mismatches = 0;
  UINT32 first = 0;
  // Both supported RGB layouts encode green identically; reserved bits are ignored.
  for (UINTN y = height - block; y < height; y++)
    for (UINTN x = width - block; x < width; x++) {
      UINT32 pixel = fb[y * mode->Info->PixelsPerScanLine + x] & 0x00ffffff;
      if (pixel != 0x0000ff00) {
        if (!mismatches) first = pixel;
        mismatches++;
      }
    }
  if (!GvMark(c, "readback-end") ||
      !c->log("VISUAL_READBACK_RESULT pixels=%zu mismatches=%zu first_mismatch=%08x matched=%u "
              "visual_confirmation=pending\n",
              (size_t)(block * block), (size_t)mismatches, first, !mismatches))
    return EFI_DEVICE_ERROR;
  observed = GxObserve(c, "after-readback");
  return mismatches ? EFI_COMPROMISED_DATA : observed;
}
