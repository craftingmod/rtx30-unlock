#include "probe_core.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/FileInfo.h>
#include "unlock/sha256.h"

static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
static EFI_SYSTEM_TABLE *St;
static CHAR8 Log[32768];
static UINTN Used;
static BOOLEAN LogFull;
/* Retain an absolute image pointer so the PE has a loader relocation table. */
static CHAR16 *volatile Banner = L"\r\nRTX 30 read-only diagnostic v1\r\n";
static VOID Hex(const CHAR8 *Key, UINT64 Value);

static VOID Text(const CHAR8 *S) {
  while (*S && Used + 1 < sizeof(Log)) Log[Used++] = *S++;
  if (*S) LogFull = TRUE;
  Log[Used] = 0;
}
static VOID Section(const CHAR8 *Name, UINTN Index) {
  static const CHAR8 Digits[] = "0123456789abcdef";
  CHAR8 Number[5];
  UINTN I;
  for (I = 0; I < 4; I++) Number[I] = Digits[(Index >> ((3 - I) * 4)) & 15];
  Number[4] = 0;
  Text("["); Text(Name); Text(":"); Text(Number); Text("]\r\n");
}
static VOID ConfigDump(const UINT32 *Config) {
  static const CHAR8 Digits[] = "0123456789abcdef";
  UINTN I;
  for (I = 0; I < 64; I++) {
    CHAR8 Key[] = "config_00";
    Key[7] = Digits[(I * 4) >> 4]; Key[8] = Digits[(I * 4) & 15];
    Hex(Key, Config[I]);
  }
}
static VOID Hash(const CHAR8 *Key, const VOID *Data, UINTN Size) {
  static const CHAR8 Digits[] = "0123456789abcdef";
  UINT8 Digest[32];
  CHAR8 Value[65];
  UINTN I;
  GuSha256(Data, Size, Digest);
  for (I = 0; I < 32; I++) {
    Value[I * 2] = Digits[Digest[I] >> 4]; Value[I * 2 + 1] = Digits[Digest[I] & 15];
  }
  Value[64] = 0; Text(Key); Text("="); Text(Value); Text("\r\n");
}
static VOID Hex(const CHAR8 *Key, UINT64 Value) {
  static const CHAR8 Digits[] = "0123456789abcdef";
  INTN I;
  Text(Key); Text("=0x");
  for (I = 60; I >= 0; I -= 4) {
    CHAR8 S[2] = {Digits[(Value >> I) & 15], 0}; Text(S);
  }
  Text("\r\n");
}
static VOID Say(CHAR16 *S) {
  if (St->ConOut && St->ConOut->OutputString) St->ConOut->OutputString(St->ConOut, S);
}
static VOID ShowLog(VOID) {
  CHAR16 Line[128];
  UINTN I = 0;
  while (I < Used) {
    UINTN N = 0;
    while (I < Used && N < 127) Line[N++] = (CHAR16)(UINT8)Log[I++];
    Line[N] = 0; Say(Line);
  }
}

/* Bounded walk of firmware-owned device-path nodes, without opening controllers. */
static BOOLEAN PathHasUsb(EFI_HANDLE Handle, BOOLEAN Record) {
  EFI_DEVICE_PATH_PROTOCOL *Path = NULL;
  UINTN Total = 0, Nodes = 0;
  BOOLEAN Usb = FALSE;
  EFI_STATUS Status = St->BootServices->HandleProtocol(Handle, &PathGuid, (VOID **)&Path);
  if (Record) Hex("device_path_status", Status);
  if (EFI_ERROR(Status) || Path == NULL) return FALSE;
  while (Nodes++ < 64 && Total + 4 <= 4096) {
    UINTN Length = Path->Length[0] | ((UINTN)Path->Length[1] << 8);
    UINTN I;
    if (Length < 4 || Length > 4096 - Total) break;
    if (Path->Type == MESSAGING_DEVICE_PATH &&
        (Path->SubType == MSG_USB_DP || Path->SubType == MSG_USB_CLASS_DP ||
         Path->SubType == MSG_USB_WWID_DP)) Usb = TRUE;
    if (Record) {
      static const CHAR8 Digits[] = "0123456789abcdef";
      Text("path_node=");
      for (I = 0; I < Length; I++) {
        UINT8 B = ((UINT8 *)Path)[I];
        CHAR8 Pair[3] = {Digits[B >> 4], Digits[B & 15], 0}; Text(Pair);
      }
      Text("\r\n");
    }
    if (Path->Type == END_DEVICE_PATH_TYPE)
      return Usb && Length == 4 && Path->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE;
    Total += Length;
    Path = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Path + Length);
  }
  return FALSE; /* Malformed or unterminated path is never an output destination. */
}

/* Only a USB-loaded image with our existing marker directory can create files. */
static EFI_FILE_PROTOCOL *OpenOutput(EFI_HANDLE Image) {
  EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_FILE_PROTOCOL *Root = NULL, *Parent = NULL, *Run = NULL;
  EFI_STATUS Status;
  UINTN Slot;
  union { EFI_FILE_INFO Info; UINT8 Bytes[512]; } Metadata;
  UINTN MetadataSize = sizeof(Metadata);
  Status = St->BootServices->HandleProtocol(Image, &LoadedGuid, (VOID **)&Loaded);
  Hex("loaded_image_status", Status);
  if (EFI_ERROR(Status) || Loaded == NULL) return NULL;
  if (!PathHasUsb(Loaded->DeviceHandle, TRUE)) {
    Text("output=disabled: loaded image device is not a verified USB path\r\n");
    return NULL;
  }
  Status = St->BootServices->HandleProtocol(Loaded->DeviceHandle, &FsGuid, (VOID **)&Fs);
  Hex("usb_filesystem_status", Status);
  if (EFI_ERROR(Status) || Fs == NULL) return NULL;
  Status = Fs->OpenVolume(Fs, &Root);
  Hex("usb_open_volume_status", Status);
  if (EFI_ERROR(Status) || Root == NULL) return NULL;
  Status = Root->Open(Root, &Parent, L"\\RTX30-PROBE",
                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  Root->Close(Root);
  Hex("probe_directory_status", Status);
  if (EFI_ERROR(Status) || Parent == NULL) return NULL;
  Status = Parent->GetInfo(Parent, &FileInfoGuid, &MetadataSize, &Metadata);
  if (EFI_ERROR(Status) || MetadataSize < SIZE_OF_EFI_FILE_INFO ||
      !(Metadata.Info.Attribute & EFI_FILE_DIRECTORY)) {
    Text("output=disabled: marker is not a verified directory\r\n");
    Parent->Close(Parent); return NULL;
  }
  for (Slot = 0; Slot < 100; Slot++) {
    CHAR16 Name[] = L"RUN00";
    Name[3] = (CHAR16)('0' + Slot / 10); Name[4] = (CHAR16)('0' + Slot % 10);
    Status = Parent->Open(Parent, &Run, Name, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status)) { Run->Close(Run); Run = NULL; continue; }
    if (Status != EFI_NOT_FOUND) break;
    Status = Parent->Open(Parent, &Run, Name,
      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
    Hex("run_directory_status", Status); Hex("run_slot", Slot);
    if (EFI_ERROR(Status)) Run = NULL;
    break;
  }
  Parent->Close(Parent);
  return Run;
}

static EFI_STATUS Save(EFI_FILE_PROTOCOL *Directory, CHAR16 *Name,
                       VOID *Data, UINTN Size) {
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS Status;
  UINTN Written = Size;
  if (!Directory) return EFI_NOT_FOUND;
  Status = Directory->Open(Directory, &File, Name, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR(Status)) { File->Close(File); return EFI_ALREADY_STARTED; }
  if (Status != EFI_NOT_FOUND) return Status;
  Status = Directory->Open(Directory, &File, Name,
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(Status)) return Status;
  Status = File->Write(File, &Written, Data);
  if (!EFI_ERROR(Status) && Written != Size) Status = EFI_VOLUME_FULL;
  if (!EFI_ERROR(Status)) Status = File->Flush(File);
  File->Close(File);
  return Status;
}

static VOID RecordGop(VOID) {
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, I;
  EFI_STATUS Status = St->BootServices->LocateHandleBuffer(ByProtocol, &GopGuid,
                                                        NULL, &Count, &Handles);
  Hex("gop_enumeration_status", Status);
  if (EFI_ERROR(Status)) return;
  for (I = 0; I < Count; I++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    Status = St->BootServices->HandleProtocol(Handles[I], &GopGuid, (VOID **)&Gop);
    if (EFI_ERROR(Status) || !Gop || !Gop->Mode) continue;
    Section("GOP", I); Hex("index", I);
    Hex("console_handle", Handles[I] == St->ConsoleOutHandle);
    Hex("mode", Gop->Mode->Mode);
    Hex("framebuffer_base", Gop->Mode->FrameBufferBase);
    Hex("framebuffer_size", Gop->Mode->FrameBufferSize);
    if (Gop->Mode->Info && Gop->Mode->SizeOfInfo >= sizeof(*Gop->Mode->Info)) {
      Hex("width", Gop->Mode->Info->HorizontalResolution);
      Hex("height", Gop->Mode->Info->VerticalResolution);
      Hex("pixels_per_scanline", Gop->Mode->Info->PixelsPerScanLine);
      Hex("pixel_format", Gop->Mode->Info->PixelFormat);
    }
    PathHasUsb(Handles[I], TRUE);
  }
  if (Handles) St->BootServices->FreePool(Handles);
}

static VOID RecordBars(EFI_PCI_IO_PROTOCOL *Pci, UINTN Index) {
  UINTN Bar;
  for (Bar = 0; Bar < 6; Bar++) {
    EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Descriptor = NULL;
    EFI_STATUS Status = Pci->GetBarAttributes(Pci, (UINT8)Bar, NULL, (VOID **)&Descriptor);
    Section("BAR", Index * 8 + Bar);
    Hex("pci_index", Index); Hex("bar_index", Bar);
    if (!EFI_ERROR(Status)) {
      if (!Descriptor || Descriptor->Desc != ACPI_ADDRESS_SPACE_DESCRIPTOR ||
          Descriptor->Len < sizeof(*Descriptor) - 3 || Descriptor->AddrLen == 0)
        Status = EFI_COMPROMISED_DATA;
      else {
        Hex("resource_type", Descriptor->ResType);
        Hex("base", Descriptor->AddrRangeMin); Hex("length", Descriptor->AddrLen);
      }
    }
    Hex("status", Status);
    if (Descriptor) St->BootServices->FreePool(Descriptor);
  }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *Table) {
  EFI_BOOT_SERVICES *Bs;
  EFI_HANDLE *Handles = NULL, SelectedHandle = NULL;
  EFI_PCI_IO_PROTOCOL *Selected = NULL;
  EFI_FILE_PROTOCOL *Output;
  UINTN Count = 0, I, Matches = 0;
  EFI_STATUS Status;
  PROBE_RESULT Result;
  UINTN SelectedIndex = 0;
  St = Table; Bs = St->BootServices; Used = 0; Log[0] = 0; LogFull = FALSE;
  Say(Banner);
  Say(L"Collects BIOS and preboot state. This does not unlock the GPU.\r\n");
  Text("[REPORT]\r\n"); Hex("format", 1);
  Text("[OUTPUT]\r\n");
  Output = OpenOutput(Image);
  Status = Bs->LocateHandleBuffer(ByProtocol, &PciGuid, NULL, &Count, &Handles);
  Hex("pci_enumeration_status", Status);
  if (!EFI_ERROR(Status)) {
    for (I = 0; I < Count; I++) {
      EFI_PCI_IO_PROTOCOL *Pci = NULL;
      UINT32 Config[64];
      UINTN Segment = 0, Bus = 0, Device = 0, Function = 0;
      Status = Bs->HandleProtocol(Handles[I], &PciGuid, (VOID **)&Pci);
      if (EFI_ERROR(Status) || Pci == NULL) continue;
      Status = Pci->Pci.Read(Pci, EfiPciIoWidthUint32, 0, 64, Config);
      if (EFI_ERROR(Status) || !ProbeMatches(Config)) continue;
      Section("PCI", Matches);
      Status = Pci->GetLocation(Pci, &Segment, &Bus, &Device, &Function);
      Hex("location_status", Status); Hex("segment", Segment); Hex("bus", Bus);
      Hex("device", Device); Hex("function", Function);
      Hex("identity", Config[0]); Hex("class_revision", Config[2]);
      Hex("subsystem", Config[11]);
      Hex("detail_supported", ProbeSupportsDetail(Config));
      ConfigDump(Config); PathHasUsb(Handles[I], TRUE);
      RecordBars(Pci, Matches);
      SelectedIndex = Matches++;
      Selected = Pci; SelectedHandle = Handles[I];
    }
  }
  if (Handles) Bs->FreePool(Handles);
  Text("[SUMMARY]\r\n"); Hex("matching_cards", Matches);
  RecordGop();
  /* Persist inventory before touching MMIO; a failed log must not start detail reads. */
  Status = Save(Output, L"INVENTORY.TXT", Log, Used);
  Text("[DETAIL]\r\n"); Hex("inventory_save_status", Status);
  if (Matches == 1 && !EFI_ERROR(Status) && !LogFull) {
    Say(L"One NVIDIA display device found. Checking read-only allowlist...\r\n");
    Text("[TARGET]\r\n"); Hex("pci_index", SelectedIndex); PathHasUsb(SelectedHandle, TRUE);
    Status = ProbeCollect(Selected, Bs, &Result);
    Hex("collect_status", Status);
    if (!EFI_ERROR(Status)) {
      Hex("identity", Result.Config[0]); Hex("subsystem", Result.Config[11]);
      Hex("bar0_status", Result.BarStatus); Hex("bar0_length", Result.BarSize);
      Hex("bar0_base", Result.BarBase);
      Hex("memory_decode_enabled", Result.MemoryEnabled);
      for (I = 0; I < PROBE_SAMPLE_COUNT; I++) {
        PROBE_SAMPLE *Sample = &Result.Samples[I];
        Section("REG", I);
        Hex("register", Sample->Offset); Hex("status", Sample->Status); Hex("value", Sample->Value);
      }
      Text("[DUMPS]\r\n");
      Hex("rom_image_size", Selected->RomSize);
      if (Output && ProbeRomUsable(Selected->RomImage, Selected->RomSize)) {
        Status = Save(Output, L"ROMIMAGE.BIN", Selected->RomImage, (UINTN)Selected->RomSize);
        Hex("rom_image_save_status", Status);
      } else Text("rom_image=not saved (no writable USB or missing/invalid firmware ROM copy)\r\n");
      if (Output && Result.MemoryEnabled && !EFI_ERROR(Result.BarStatus) &&
          Result.BarSize >= PROBE_PROM_OFFSET + PROBE_PROM_SIZE) {
        VOID *Prom = NULL;
        Status = Bs->AllocatePool(EfiLoaderData, PROBE_PROM_SIZE, &Prom);
        Hex("prom_allocation_status", Status);
        if (!EFI_ERROR(Status) && Prom) {
          Say(L"Reading the 1 MiB PROM window; no ROM-enable writes are used.\r\n");
          Status = ProbeReadProm(Selected, &Result, Prom, PROBE_PROM_SIZE);
          Hex("prom_read_status", Status);
          if (!EFI_ERROR(Status) && ProbeRomUsable(Prom, PROBE_PROM_SIZE)) {
            Hash("prom_sha256", Prom, PROBE_PROM_SIZE);
            Hex("prom_size", PROBE_PROM_SIZE);
            Status = Save(Output, L"PROM.BIN", Prom, PROBE_PROM_SIZE);
            Hex("prom_save_status", Status);
          } else Text("prom=not saved (read error or no 55aa ROM signature)\r\n");
          Bs->FreePool(Prom);
        }
      }
    }
  } else {
    Text("[SKIPPED]\r\nreason=missing/ambiguous target or unavailable inventory log\r\n");
    Say(L"Detail reads skipped: target ambiguous/missing or log unavailable.\r\n");
  }
  Text("[END]\r\ngpu_writes=none\r\ngpu_resets=none\r\nboot_variable_changes=none\r\n");
  Status = LogFull ? EFI_BUFFER_TOO_SMALL : Save(Output, L"REPORT.TXT", Log, Used);
  if (Output) Output->Close(Output);
  if (EFI_ERROR(Status)) {
    Say(L"Could not save report. Diagnostic details follow:\r\n");
    Hex("report_save_status", Status); ShowLog();
  } else Say(L"Saved: USB \\RTX30-PROBE\\RUNxx\\REPORT.TXT and available BIOS copies.\r\n");
  Say(L"Returning in 15 seconds. Choose Windows Boot Manager if a boot menu appears.\r\n");
  Bs->Stall(15000000);
  return EFI_SUCCESS;
}
