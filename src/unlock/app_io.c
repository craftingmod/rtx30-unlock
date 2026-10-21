#include "app_io.h"
#include "compat/compat.h"
#include "boot_source.h"
#include "format.h"
#include "sha256.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
static EFI_GUID LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID,
                FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, InfoGuid = EFI_FILE_INFO_ID;
static EFI_SYSTEM_TABLE *Table;
static EFI_FILE_PROTOCOL *Root, *Parent, *Run, *Report;
static BOOLEAN LogFault, Quiet;
VOID GuIoSetQuiet(BOOLEAN quiet) { Quiet = quiet; }
typedef struct {
  const char *name;
  CHAR16 *file;
  UINTN size;
  const char *hash;
} ASSET_SPEC;
static const ASSET_SPEC Specs[] = {
    {"gpu-unlock/610.43.03/booter_load_ga102.bin", L"ASSETS\\booter_load_ga102.bin", 61256,
     "056cbc064a888f80717726c54d7f64fcd68044d123870c95a74f37f4975dcaa4"},
    {"gpu-unlock/610.43.03/v67-ga102-610.43.03.bin", L"ASSETS\\v67-ga102-610.43.03.bin", 64000,
     "2a0aa1f86d0b6afff5214d641d87d46dc98ef06ed908ff05d0ddbad5ffb7e197"},
    {"gpu-unlock/610.43.03/gsp_ga10x.bin", L"ASSETS\\gsp_ga10x.bin", 84277400,
     "572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a"},
    {"gpu-unlock/610.43.03/gsp_boot_ga102.bin", L"ASSETS\\gsp_boot_ga102.bin", 24576,
     "3df651acb174fd5f5561a1996965b192f50c5d763168446ed0d4ead19d895768"},
    {"gpu-unlock/610.43.03/gsp_boot_desc_ga102.bin", L"ASSETS\\gsp_boot_desc_ga102.bin", 84,
     "cc5a3fc1f4c47f2fe107b1b68f0176d8a448e6dec84b575e5560ccef5262bf15"}};
static VOID LogError(VOID) {
  LogFault = TRUE;
  if (Gu.armed) Gu.fault = TRUE;
}
VOID GuConsole(const char *Text) {
#ifdef GU_GOP_EXPERIMENT
  (void)Text;
  LogError();
#else
  if (!Table || !Table->ConOut || !Table->ConOut->OutputString) {
    LogError();
    return;
  }
  while (*Text) {
    CHAR16 line[128];
    UINTN n = 0;
    while (*Text && n < 126) {
      if (*Text == '\n') line[n++] = '\r';
      line[n++] = (UINT8)*Text++;
    }
    line[n] = 0;
    if (EFI_ERROR(Table->ConOut->OutputString(Table->ConOut, line))) {
      LogError();
      return;
    }
  }
#endif
}
VOID GuLogText(const char *Text) {
  if (Quiet) return;
#ifndef GU_GOP_EXPERIMENT
  GuConsole(Text);
#endif
  if (!Report || LogFault) {
    LogError();
    return;
  }
  UINTN expected = strlen(Text), written = expected;
  EFI_STATUS status = Report->Write(Report, &written, (VOID *)Text);
  if (EFI_ERROR(status) || written != expected || EFI_ERROR(Report->Flush(Report))) LogError();
}
VOID GuLog(const char *Format, ...) {
  if (Quiet) return;
  char text[1024];
  va_list args;
  va_start(args, Format);
  size_t n = GuFormatV(text, sizeof(text), Format, args);
  va_end(args);
  if (n >= sizeof(text)) {
    LogError();
    GuConsole("Log format or length error.\n");
    return;
  }
  GuLogText(text);
}
BOOLEAN GuLogReady(VOID) { return (Quiet || Report) && !LogFault; }
static BOOLEAN UsbPath(EFI_HANDLE Handle) { return GuBootSourceAllowed(Table, Handle); }
static EFI_STATUS Metadata(EFI_FILE_PROTOCOL *File, BOOLEAN Directory, UINTN Expected) {
  union {
    EFI_FILE_INFO info;
    UINT8 bytes[512];
  } data;
  UINTN n = sizeof(data);
  EFI_STATUS status = File->GetInfo(File, &InfoGuid, &n, &data);
  if (EFI_ERROR(status)) return status;
  if (n < SIZE_OF_EFI_FILE_INFO || n > sizeof(data) || data.info.Size > n ||
      data.info.Size < SIZE_OF_EFI_FILE_INFO ||
      !!(data.info.Attribute & EFI_FILE_DIRECTORY) != !!Directory ||
      (!Directory && data.info.FileSize != Expected))
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}
static EFI_STATUS IoOpen(EFI_HANDLE Image, EFI_SYSTEM_TABLE *St, BOOLEAN Shared, UINTN Selected) {
  Table = St;
  Root = Parent = Run = Report = NULL;
  LogFault = FALSE;
  memset(GuAssets, 0, sizeof(GuAssets));
  if (Shared && !Quiet && Selected >= 100) return EFI_INVALID_PARAMETER;
  EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
  EFI_STATUS status = Table->BootServices->HandleProtocol(Image, &LoadedGuid, (VOID **)&loaded);
  if (EFI_ERROR(status) || !loaded || !UsbPath(loaded->DeviceHandle)) return EFI_UNSUPPORTED;
  status = Table->BootServices->HandleProtocol(loaded->DeviceHandle, &FsGuid, (VOID **)&fs);
  if (EFI_ERROR(status) || !fs) return EFI_UNSUPPORTED;
  status = fs->OpenVolume(fs, &Root);
  if (EFI_ERROR(status) || !Root) return EFI_DEVICE_ERROR;
  status = Root->Open(Root, &Parent, L"\\RTX-UNLOCK", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (EFI_ERROR(status) || !Parent) return EFI_NOT_FOUND;
  status = Metadata(Parent, TRUE, 0);
  if (EFI_ERROR(status)) return status;
  if (Quiet) return EFI_SUCCESS;
  for (UINTN slot = Shared ? Selected : 0; slot < (Shared ? Selected + 1 : 100); slot++) {
    CHAR16 name[] = L"RUN00";
    name[3] = (CHAR16)('0' + slot / 10);
    name[4] = (CHAR16)('0' + slot % 10);
    EFI_FILE_PROTOCOL *existing = NULL;
    status =
        Parent->Open(Parent, &existing, name,
                     Shared ? (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE) : EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
      if (!existing) return EFI_DEVICE_ERROR;
      if (Shared)
        Run = existing;
      else {
        if (EFI_ERROR(existing->Close(existing))) return EFI_DEVICE_ERROR;
        continue;
      }
    } else {
      if (status != EFI_NOT_FOUND || Shared) return status;
      status = Parent->Open(Parent, &Run, name,
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                            EFI_FILE_DIRECTORY);
      if (EFI_ERROR(status) || !Run) return EFI_DEVICE_ERROR;
    }
    existing = NULL;
    status = Metadata(Run, TRUE, 0);
    if (EFI_ERROR(status)) return status;
    status = Run->Open(Run, &existing, L"UNLOCK.TXT", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
      if (existing) existing->Close(existing);
      return EFI_ALREADY_STARTED;
    }
    if (status != EFI_NOT_FOUND) return status;
    status = Run->Open(Run, &Report, L"UNLOCK.TXT",
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(status) || !Report) return EFI_DEVICE_ERROR;
    GuLog("RTX GA10x UEFI unlock experiment\nRUN%02u\nBus Master protocol state checks; hardware "
          "fix unproven; one manual attempt per launch.\n",
          (unsigned)slot);
    GuLogText("PIO readback is not an execution gate. This path does not independently verify TCM "
              "contents; FWSEC scratch/WPR results are required.\n");
    return GuLogReady() ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  }
  return EFI_VOLUME_FULL;
}
EFI_STATUS GuIoOpen(EFI_HANDLE Image, EFI_SYSTEM_TABLE *St) { return IoOpen(Image, St, FALSE, 0); }
EFI_STATUS GuIoOpenRun(EFI_HANDLE Image, EFI_SYSTEM_TABLE *St, UINTN Slot) {
  return IoOpen(Image, St, TRUE, Slot);
}
EFI_STATUS GuLoadAssets(VOID) {
  for (UINTN i = 0; i < 5; i++) {
    EFI_FILE_PROTOCOL *file = NULL;
    const ASSET_SPEC *spec = &Specs[i];
    EFI_STATUS status = Parent->Open(Parent, &file, spec->file, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !file) return EFI_NOT_FOUND;
    status = Metadata(file, FALSE, spec->size);
    UINT8 *data = NULL;
    if (!EFI_ERROR(status)) {
      data = GuAlloc(spec->size);
      if (!data) status = EFI_OUT_OF_RESOURCES;
    }
    for (UINTN done = 0; !EFI_ERROR(status) && done < spec->size;) {
      UINTN n = spec->size - done;
      if (n > 1048576) n = 1048576;
      UINTN want = n;
      status = file->Read(file, &n, data + done);
      if (!EFI_ERROR(status) && n != want) status = EFI_END_OF_FILE;
      done += n;
    }
    EFI_STATUS closed = file->Close(file);
    if (!EFI_ERROR(status)) status = closed;
    if (!EFI_ERROR(status) && !GuHashMatches(data, spec->size, spec->hash)) status = EFI_CRC_ERROR;
    if (EFI_ERROR(status)) {
      GuFree(data);
      return status;
    }
    GuAssets[i].name = spec->name;
    GuAssets[i].firmware.data = data;
    GuAssets[i].firmware.size = spec->size;
    GuLog("asset verified: %s bytes=%zu sha256=%s\n", spec->name, (size_t)spec->size, spec->hash);
    if (!GuLogReady()) return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
VOID GuReleaseAssets(VOID) {
  if (!GuSafeToReturn()) return;
  for (UINTN i = 0; i < 5; i++) {
    GuFree((VOID *)GuAssets[i].firmware.data);
    memset(&GuAssets[i], 0, sizeof(GuAssets[i]));
  }
}
EFI_STATUS GuIoClose(VOID) {
  EFI_FILE_PROTOCOL **files[] = {&Report, &Run, &Parent, &Root};
  EFI_STATUS result = EFI_SUCCESS;
  if (!GuSafeToReturn()) return EFI_ACCESS_DENIED;
  for (UINTN i = 0; i < 4; i++)
    if (*files[i]) {
      /* EFI_FILE_PROTOCOL.Close invalidates the handle even on error. */
      EFI_FILE_PROTOCOL *file = *files[i];
      *files[i] = NULL;
      EFI_STATUS status = file->Close(file);
      if (EFI_ERROR(status)) return status;
    }
  return result;
}
