#include "windows.h"
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <string.h>
static EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
                PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID, InfoGuid = EFI_FILE_INFO_ID;
static const CHAR16 WindowsPath[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
/* Only single-instance disk paths are candidates; never chainload from USB. */
static UINTN DiskPathBytes(const EFI_DEVICE_PATH_PROTOCOL *path) {
  const UINT8 *p = (const UINT8 *)path;
  UINTN bytes = 0;
  BOOLEAN disk = FALSE;
  if (!path) return 0;
  for (UINTN i = 0; i < 64 && bytes + 4 <= 4096; i++) {
    const EFI_DEVICE_PATH_PROTOCOL *node = (const VOID *)(p + bytes);
    UINTN length = node->Length[0] + ((UINTN)node->Length[1] << 8);
    if (length < 4 || length > 4096 - bytes) return 0;
    if (node->Type == MESSAGING_DEVICE_PATH &&
        (node->SubType == MSG_USB_DP || node->SubType == MSG_USB_CLASS_DP ||
         node->SubType == MSG_USB_WWID_DP))
      return 0;
    if (node->Type == MEDIA_DEVICE_PATH && node->SubType == MEDIA_HARDDRIVE_DP) {
      if (length != sizeof(HARDDRIVE_DEVICE_PATH)) return 0;
      disk = TRUE;
    }
    if (node->Type == END_DEVICE_PATH_TYPE)
      return disk && length == 4 && node->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE ? bytes + 4 : 0;
    bytes += length;
  }
  return 0;
}
EFI_STATUS GwCancel(GC_CONTEXT *c, GW_PLAN *p) {
  if (!p->image) return EFI_SUCCESS;
  EFI_STATUS s = c->table->BootServices->UnloadImage(p->image);
  if (s == EFI_SUCCESS) p->image = NULL;
  return s;
}
EFI_STATUS GwPrepare(GC_CONTEXT *c, GW_PLAN *p) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_HANDLE *handles = NULL, selected = NULL;
  EFI_DEVICE_PATH_PROTOCOL *selectedPath = NULL;
  UINTN selectedBytes = 0, count = 0, matches = 0;
  if (p->image || !bs->LoadImage || !bs->StartImage || !bs->UnloadImage) return EFI_UNSUPPORTED;
  EFI_STATUS s = bs->LocateHandleBuffer(ByProtocol, &FsGuid, NULL, &count, &handles);
  if (s != EFI_SUCCESS) return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
  if (!handles || !count || count > 128) s = EFI_UNSUPPORTED;
  for (UINTN i = 0; s == EFI_SUCCESS && i < count; i++) {
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_STATUS found = bs->HandleProtocol(handles[i], &PathGuid, (VOID **)&path);
    UINTN bytes = found == EFI_SUCCESS ? DiskPathBytes(path) : 0;
    if (!bytes) continue;
    found = bs->HandleProtocol(handles[i], &FsGuid, (VOID **)&fs);
    if (found != EFI_SUCCESS || !fs || !fs->OpenVolume) {
      s = EFI_DEVICE_ERROR;
      break;
    }
    EFI_FILE_PROTOCOL *root = NULL, *file = NULL;
    found = fs->OpenVolume(fs, &root);
    if (found != EFI_SUCCESS || !root) {
      s = EFI_DEVICE_ERROR;
      break;
    }
    found = root->Open(root, &file, (CHAR16 *)WindowsPath, EFI_FILE_MODE_READ, 0);
    EFI_STATUS closed = root->Close(root);
    if (EFI_ERROR(closed)) {
      if (file) file->Close(file);
      s = closed;
      break;
    }
    if (found == EFI_NOT_FOUND) continue;
    if (found != EFI_SUCCESS || !file) {
      if (file) file->Close(file);
      s = EFI_ERROR(found) ? found : EFI_DEVICE_ERROR;
      break;
    }
    union {
      EFI_FILE_INFO info;
      UINT8 buffer[1024];
    } metadata;
    UINTN size = sizeof(metadata);
    found = file->GetInfo(file, &InfoGuid, &size, &metadata);
    closed = file->Close(file);
    if (found != EFI_SUCCESS || EFI_ERROR(closed) || size < SIZE_OF_EFI_FILE_INFO ||
        (metadata.info.Attribute & EFI_FILE_DIRECTORY) || !metadata.info.FileSize) {
      s = EFI_COMPROMISED_DATA;
      break;
    }
    matches++;
    selected = handles[i];
    selectedPath = path;
    selectedBytes = bytes;
    if (!c->log("WINDOWS_CANDIDATE device=%#llx file_bytes=%llu "
                "path=\\EFI\\Microsoft\\Boot\\bootmgfw.efi\n",
                (unsigned long long)(UINTN)selected, (unsigned long long)metadata.info.FileSize))
      s = EFI_DEVICE_ERROR;
  }
  if (!c->log("WINDOWS_SCAN status=%#llx matches=%zu\n", (unsigned long long)s, (size_t)matches))
    s = EFI_DEVICE_ERROR;
  if (s == EFI_SUCCESS && matches != 1) s = matches ? EFI_UNSUPPORTED : EFI_NOT_FOUND;
  EFI_DEVICE_PATH_PROTOCOL *full = NULL;
  if (s == EFI_SUCCESS) {
    UINTN fileBytes = 4 + sizeof(WindowsPath), total = selectedBytes + fileBytes;
    s = bs->AllocatePool(EfiLoaderData, total, (VOID **)&full);
    if (s == EFI_SUCCESS && !full) s = EFI_OUT_OF_RESOURCES;
    if (s == EFI_SUCCESS) {
      memcpy(full, selectedPath, selectedBytes - 4);
      EFI_DEVICE_PATH_PROTOCOL *file = (VOID *)((UINT8 *)full + selectedBytes - 4);
      file->Type = MEDIA_DEVICE_PATH;
      file->SubType = MEDIA_FILEPATH_DP;
      file->Length[0] = (UINT8)fileBytes;
      file->Length[1] = (UINT8)(fileBytes >> 8);
      memcpy((UINT8 *)file + 4, WindowsPath, sizeof(WindowsPath));
      EFI_DEVICE_PATH_PROTOCOL *end = (VOID *)((UINT8 *)file + fileBytes);
      end->Type = END_DEVICE_PATH_TYPE;
      end->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
      end->Length[0] = 4;
      end->Length[1] = 0;
      if (!c->log("WINDOWS_LOAD_INTENT device=%#llx boot_policy=0\n",
                  (unsigned long long)(UINTN)selected))
        s = EFI_DEVICE_ERROR;
      else
        s = bs->LoadImage(FALSE, c->image, full, NULL, 0, &p->image);
      if (!c->log("WINDOWS_LOAD_RESULT status=%#llx image=%#llx\n", (unsigned long long)s,
                  (unsigned long long)(UINTN)p->image))
        s = EFI_DEVICE_ERROR;
      if (s == EFI_SUCCESS && !p->image) s = EFI_COMPROMISED_DATA;
      if (s == EFI_SUCCESS) {
        EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
        s = bs->HandleProtocol(p->image, &LoadedGuid, (VOID **)&loaded);
        if (s == EFI_SUCCESS &&
            (!loaded || loaded->DeviceHandle != selected || loaded->ImageCodeType != EfiLoaderCode))
          s = EFI_COMPROMISED_DATA;
      }
    }
  }
  if (full && EFI_ERROR(bs->FreePool(full))) s = EFI_DEVICE_ERROR;
  if (handles && EFI_ERROR(bs->FreePool(handles))) s = EFI_DEVICE_ERROR;
  if (s != EFI_SUCCESS) {
    EFI_STATUS cleanup = GwCancel(c, p);
    if (EFI_ERROR(cleanup)) return cleanup;
  }
  return s;
}
EFI_STATUS GwStart(GC_CONTEXT *c, GW_PLAN *p, EFI_FILE_PROTOCOL **report, BOOLEAN verified) {
  if (!verified || !p->image || !report || (!c->quiet && !*report)) return EFI_ACCESS_DENIED;
  if (!c->log("WINDOWS_START_INTENT image=%#llx reboot=0 reports_close_before_start=1 "
              "success_requires_OS_observation=1\n",
              (unsigned long long)(UINTN)p->image))
    return EFI_DEVICE_ERROR;
  EFI_STATUS s = *report ? (*report)->Close(*report) : EFI_SUCCESS;
  *report = NULL;
  if (s != EFI_SUCCESS) return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
  s = c->table->BootServices->StartImage(p->image, NULL, NULL);
  // A return, even EFI_SUCCESS, is not a successful Windows handoff.
  return s == EFI_SUCCESS ? EFI_ABORTED : s;
}
