#include "probe_core.h"
#include "gpu_family.h"

static const UINT32 Offsets[PROBE_SAMPLE_COUNT] = {
    0x000000, 0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828, 0x823804, 0x82381c,
    0x823820, 0x1183a4, 0x625f04, 0x1fa7c8, 0x1fa82c, 0x1fa830, 0x824148, 0x8241e0};

BOOLEAN ProbeMatches(const UINT32 *Config) {
  return Config != NULL && (Config[0] & 0xffffU) == 0x10deU && (Config[2] >> 24) == 3;
}

BOOLEAN ProbeSupportsDetail(const UINT32 *Config) {
  return ProbeMatches(Config) && GpuGa10xPciCandidate(Config[0]);
}

EFI_STATUS ProbeCollect(EFI_PCI_IO_PROTOCOL *Pci, EFI_BOOT_SERVICES *Bs, PROBE_RESULT *Result) {
  EFI_STATUS Status;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Descriptor = NULL;
  UINTN I;
  if (Pci == NULL || Bs == NULL || Result == NULL) return EFI_INVALID_PARAMETER;
  *Result = (PROBE_RESULT){0};
  Status = Pci->Pci.Read(Pci, EfiPciIoWidthUint32, 0, 64, Result->Config);
  if (EFI_ERROR(Status)) return Status;
  if (!ProbeSupportsDetail(Result->Config)) return EFI_UNSUPPORTED;
  Result->MemoryEnabled = (Result->Config[1] & 2) != 0;
  Status = Pci->GetBarAttributes(Pci, 0, NULL, (VOID **)&Descriptor);
  if (!EFI_ERROR(Status)) {
    if (Descriptor == NULL || Descriptor->Desc != ACPI_ADDRESS_SPACE_DESCRIPTOR ||
        Descriptor->Len < sizeof(*Descriptor) - 3 ||
        Descriptor->ResType != ACPI_ADDRESS_SPACE_TYPE_MEM || Descriptor->AddrLen == 0 ||
        Descriptor->AddrRangeMin > MAX_UINT64 - (Descriptor->AddrLen - 1)) {
      Status = EFI_COMPROMISED_DATA;
    } else {
      Result->BarSize = Descriptor->AddrLen;
      Result->BarBase = Descriptor->AddrRangeMin;
    }
  }
  if (Descriptor != NULL) Bs->FreePool(Descriptor);
  Result->BarStatus = Status;
  for (I = 0; I < PROBE_SAMPLE_COUNT; I++) {
    PROBE_SAMPLE *Sample = &Result->Samples[I];
    Sample->Offset = Offsets[I];
    if (I && (EFI_ERROR(Result->Samples[0].Status) || !GpuGa10xChip(Result->Samples[0].Value)))
      Sample->Status = EFI_UNSUPPORTED;
    else if (!Result->MemoryEnabled)
      Sample->Status = EFI_NOT_READY;
    else if (EFI_ERROR(Status))
      Sample->Status = Status;
    else if (Result->BarSize < 4 || Offsets[I] > Result->BarSize - 4)
      Sample->Status = EFI_BAD_BUFFER_SIZE;
    else
      Sample->Status = Pci->Mem.Read(Pci, EfiPciIoWidthUint32, 0, Offsets[I], 1, &Sample->Value);
    if (!EFI_ERROR(Sample->Status) &&
        (Sample->Value == 0xffffffffU || (Sample->Value & 0xffff0000U) == 0xbadf0000U))
      Sample->Status = EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

EFI_STATUS ProbeReadProm(EFI_PCI_IO_PROTOCOL *Pci, const PROBE_RESULT *Result, VOID *Buffer,
                         UINTN Size) {
  UINTN Done;
  if (Pci == NULL || Result == NULL || Buffer == NULL || Size == 0 || Size > PROBE_PROM_SIZE ||
      (Size & 3))
    return EFI_INVALID_PARAMETER;
  if (!ProbeSupportsDetail(Result->Config)) return EFI_UNSUPPORTED;
  for (Done = 0; Done < PROBE_SAMPLE_COUNT; Done++)
    if (EFI_ERROR(Result->Samples[Done].Status)) return EFI_NOT_READY;
  if (!GpuGa10xChip(Result->Samples[0].Value)) return EFI_UNSUPPORTED;
  if (!Result->MemoryEnabled) return EFI_NOT_READY;
  if (EFI_ERROR(Result->BarStatus)) return Result->BarStatus;
  if (Result->BarSize < PROBE_PROM_OFFSET || Size > Result->BarSize - PROBE_PROM_OFFSET)
    return EFI_BAD_BUFFER_SIZE;
  for (Done = 0; Done < Size;) {
    UINTN Chunk = Size - Done;
    EFI_STATUS Status;
    if (Chunk > 1024) Chunk = 1024;
    Status = Pci->Mem.Read(Pci, EfiPciIoWidthUint32, 0, PROBE_PROM_OFFSET + Done, Chunk / 4,
                           (UINT8 *)Buffer + Done);
    if (EFI_ERROR(Status)) return Status;
    Done += Chunk;
  }
  return EFI_SUCCESS;
}

BOOLEAN ProbeRomUsable(const VOID *Buffer, UINT64 Size) {
  const UINT8 *Bytes = Buffer;
  return Buffer != NULL && Size >= 512 && Size <= PROBE_ROM_LIMIT && Bytes[0] == 0x55 &&
         Bytes[1] == 0xaa;
}
