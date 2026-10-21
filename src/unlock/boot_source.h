#pragma once
#include <Uefi.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PartitionInfo.h>
#include <Guid/Gpt.h>
#include <string.h>
static inline BOOLEAN GuBootSourceAllowed(EFI_SYSTEM_TABLE *table, EFI_HANDLE device) {
  EFI_GUID pathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
  EFI_DEVICE_PATH_PROTOCOL *path = NULL;
  if (EFI_ERROR(table->BootServices->HandleProtocol(device, &pathGuid, (VOID **)&path)) || !path)
    return FALSE;
  UINTN total = 0;
  BOOLEAN usb = FALSE, ended = FALSE;
  for (UINTN i = 0; i < 64 && total + 4 <= 4096; i++) {
    UINTN n = path->Length[0] + ((UINTN)path->Length[1] << 8);
    if (n < 4 || n > 4096 - total) return FALSE;
    if (path->Type == MESSAGING_DEVICE_PATH && path->SubType == MSG_USB_DP &&
        n == sizeof(USB_DEVICE_PATH))
      usb = TRUE;
    if (path->Type == END_DEVICE_PATH_TYPE) {
      ended = n == 4 && path->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE;
      break;
    }
    total += n;
    path = (VOID *)((UINT8 *)path + n);
  }
  if (!ended) return FALSE;
  if (usb) return TRUE;
#ifdef GU_SSD_EXPERIMENT
  EFI_GUID partGuid = EFI_PARTITION_INFO_PROTOCOL_GUID;
  EFI_GUID espGuid = EFI_PART_TYPE_EFI_SYSTEM_PART_GUID;
  EFI_PARTITION_INFO_PROTOCOL *part = NULL;
  return !EFI_ERROR(table->BootServices->HandleProtocol(device, &partGuid, (VOID **)&part)) &&
         part && part->Revision == EFI_PARTITION_INFO_PROTOCOL_REVISION &&
         part->Type == PARTITION_TYPE_GPT &&
         !memcmp(&part->Info.Gpt.PartitionTypeGUID, &espGuid, sizeof(espGuid));
#else
  return FALSE;
#endif
}
