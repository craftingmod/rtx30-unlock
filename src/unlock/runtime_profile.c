#include "runtime_profile.h"
#include "sha256.h"
#include "../probe/gpu_family.h"
#include <Guid/FileInfo.h>

GU_RUNTIME_PROFILE GuProfile;
static UINT32 Le32(const UINT8 *p) {
  return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}
BOOLEAN GuProfileDecode(const UINT8 *data, UINTN size, GU_RUNTIME_PROFILE *out) {
  static const UINT8 magic[8] = {'G', 'U', 'P', 'R', 'O', 'F', '1', '7'};
  static const char hex[] = "0123456789abcdef";
  GU_RUNTIME_PROFILE p = {0};
  UINT8 digest[32];
  if (!data || !out || size != GU_RUNTIME_PROFILE_BYTES) return FALSE;
  for (UINTN i = 0; i < 8; i++)
    if (data[i] != magic[i]) return FALSE;
  if (Le32(data + 8) != 1 || Le32(data + 12) != GU_RUNTIME_PROFILE_BYTES) return FALSE;
  GuSha256(data, 144, digest);
  for (UINTN i = 0; i < 32; i++)
    if (digest[i] != data[144 + i]) return FALSE;
  p.identity = Le32(data + 16);
  p.subsystem = Le32(data + 20);
  p.chip = Le32(data + 24);
  p.class_revision = Le32(data + 28);
  p.plm = Le32(data + 32);
  p.ss0 = Le32(data + 36);
  p.ss1 = Le32(data + 40);
  p.fb_mib = Le32(data + 44);
  p.bar_size = (UINT64)Le32(data + 48) | ((UINT64)Le32(data + 52) << 32);
  for (UINTN i = 0; i < 6; i++)
    p.layout[i] = Le32(data + 56 + i * 4);
  if (!GpuGa10xPciCandidate(p.identity) || !GpuGa10xChip(p.chip) || (p.class_revision >> 24) != 3 ||
      p.fb_mib < 256 || p.fb_mib > 0x100000 || p.bar_size < 0x842000 ||
      (p.bar_size & (p.bar_size - 1)) || !(p.layout[1] & 1) || (p.layout[5] >> 4))
    return FALSE;
  for (UINTN i = 0; i < 32; i++) {
    p.prom_sha256[i * 2] = hex[data[80 + i] >> 4];
    p.prom_sha256[i * 2 + 1] = hex[data[80 + i] & 15];
  }
  *out = p;
  return TRUE;
}
EFI_STATUS GuProfileRead(EFI_FILE_PROTOCOL *directory, UINT8 *copy) {
  EFI_FILE_PROTOCOL *file = NULL;
  EFI_GUID infoGuid = EFI_FILE_INFO_ID;
  union {
    EFI_FILE_INFO info;
    UINT8 bytes[512];
  } metadata;
  UINT8 data[GU_RUNTIME_PROFILE_BYTES], extra;
  GU_RUNTIME_PROFILE parsed = {0};
  GuProfile = parsed;
  if (!directory || !directory->Open) return EFI_INVALID_PARAMETER;
  EFI_STATUS status = directory->Open(directory, &file, L"PROFILE.BIN", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status) || !file) return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
  if (!file->Close) return EFI_UNSUPPORTED;
  if (!file->GetInfo || !file->Read)
    status = EFI_UNSUPPORTED;
  else {
    UINTN size = sizeof(metadata);
    status = file->GetInfo(file, &infoGuid, &size, &metadata);
    if (!EFI_ERROR(status) &&
        (size < SIZE_OF_EFI_FILE_INFO || size > sizeof(metadata) ||
         metadata.info.Size < SIZE_OF_EFI_FILE_INFO || metadata.info.Size > size ||
         (metadata.info.Attribute & EFI_FILE_DIRECTORY) || metadata.info.FileSize != sizeof(data)))
      status = EFI_COMPROMISED_DATA;
    if (!EFI_ERROR(status)) {
      size = sizeof(data);
      status = file->Read(file, &size, data);
      if (!EFI_ERROR(status) && size != sizeof(data)) status = EFI_END_OF_FILE;
    }
    if (!EFI_ERROR(status)) {
      size = 1;
      status = file->Read(file, &size, &extra);
      if (!EFI_ERROR(status) && size) status = EFI_COMPROMISED_DATA;
    }
    if (!EFI_ERROR(status) && !GuProfileDecode(data, sizeof(data), &parsed))
      status = EFI_COMPROMISED_DATA;
  }
  EFI_STATUS closed = file->Close(file);
  if (!EFI_ERROR(status)) status = closed;
  /* Publish once, only after both the complete read and Close succeeded. */
  if (!EFI_ERROR(status)) {
    GuProfile = parsed;
    if (copy)
      for (UINTN i = 0; i < sizeof(data); i++)
        copy[i] = data[i];
  }
  /* Only Open's EFI_NOT_FOUND means absent; a disappearing/broken file is invalid. */
  return status == EFI_NOT_FOUND ? EFI_DEVICE_ERROR : status;
}

EFI_STATUS GuProfileLoad(EFI_FILE_PROTOCOL *directory) { return GuProfileRead(directory, NULL); }
