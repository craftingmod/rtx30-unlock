#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "probe_core.h"
#include "gpu_family.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

static UINT32 config[64];
static UINT64 bar_size;
static UINT32 failed_offset;
static UINT32 chip_value;
static EFI_STATUS config_status, bar_status;
static unsigned reads, writes, prom_reads;
static EFI_PCI_IO_PROTOCOL pci;
static EFI_BOOT_SERVICES bs;
static PROBE_RESULT result;
static int failures;
static unsigned console_calls, stall_calls;
static BOOLEAN enumerate_target, multiple_targets, sentinel, fail_log;
static EFI_SYSTEM_TABLE system_table;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console_out;
static EFI_GUID pci_guid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_LOADED_IMAGE_PROTOCOL loaded_image;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL filesystem;
static EFI_FILE_PROTOCOL root_dir, marker_dir, run_dir, saved_file;
static UINT8 device_path[10];
static BOOLEAN usb_output, marker_present, read_only, occupied_slot, existing_report;
static BOOLEAN marker_is_file;
static unsigned fs_opens, run_creates, file_creates, file_writes, file_flushes;
static unsigned current_file, report_writes, prom_writes, rom_writes;
static CHAR8 saved_report[32768];
static UINT8 saved_data[PROBE_PROM_SIZE];
static UINTN saved_size, saved_position;
extern EFI_STATUS EFIAPI efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *Table);
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                  \
      failures++;                                                                                  \
    }                                                                                              \
  } while (0)

static EFI_STATUS EFIAPI read_config(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w,
                                     UINT32 off, UINTN n, VOID *buf) {
  CHECK(p == &pci && w == EfiPciIoWidthUint32 && off == 0 && n == 64);
  if (!EFI_ERROR(config_status)) memcpy(buf, config, sizeof(config));
  return config_status;
}
static EFI_STATUS EFIAPI write_config(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w,
                                      UINT32 off, UINTN n, VOID *buf) {
  (void)p;
  (void)w;
  (void)off;
  (void)n;
  (void)buf;
  writes++;
  return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI get_bar(EFI_PCI_IO_PROTOCOL *p, UINT8 index, UINT64 *support, VOID **out) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d;
  CHECK(p == &pci && index < 6 && support == NULL);
  if (index != 0) return EFI_UNSUPPORTED;
  if (EFI_ERROR(bar_status)) return bar_status;
  d = calloc(1, sizeof(*d));
  d->Desc = ACPI_ADDRESS_SPACE_DESCRIPTOR;
  d->Len = (UINT16)(sizeof(*d) - 3);
  d->ResType = ACPI_ADDRESS_SPACE_TYPE_MEM;
  d->AddrLen = bar_size;
  *out = d;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI free_pool(VOID *p) {
  free(p);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_mem(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT8 index,
                                  UINT64 off, UINTN n, VOID *buf) {
  UINTN i;
  CHECK(p == &pci && w == EfiPciIoWidthUint32 && index == 0);
  CHECK(off <= bar_size && n * 4 <= bar_size - off);
  CHECK((config[1] & 2) != 0);
  reads++;
  if (off == failed_offset) return EFI_DEVICE_ERROR;
  if (off >= PROBE_PROM_OFFSET && off < PROBE_PROM_OFFSET + PROBE_PROM_SIZE) {
    prom_reads++;
    for (i = 0; i < n; i++)
      ((UINT32 *)buf)[i] = 0x44332211;
    if (off == PROBE_PROM_OFFSET) ((UINT32 *)buf)[0] = 0x0000aa55;
  } else {
    CHECK(n == 1);
    *(UINT32 *)buf = sentinel ? 0xffffffffU : off == 0 ? chip_value : 0x11223344;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_mem(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT8 index,
                                   UINT64 off, UINTN n, VOID *buf) {
  (void)p;
  (void)w;
  (void)index;
  (void)off;
  (void)n;
  (void)buf;
  writes++;
  return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI output_text(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, CHAR16 *s) {
  CHECK(p == &console_out && s != NULL);
  console_calls++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI stall(UINTN us) {
  CHECK(us <= 15000000);
  stall_calls++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI allocate(EFI_MEMORY_TYPE type, UINTN size, VOID **out) {
  CHECK(type == EfiLoaderData);
  *out = malloc(size);
  return *out ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI locate_handles(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *guid, VOID *key,
                                        UINTN *count, EFI_HANDLE **out) {
  CHECK(type == ByProtocol && key == NULL);
  if (enumerate_target && !memcmp(guid, &pci_guid, sizeof(pci_guid))) {
    *count = multiple_targets ? 2 : 1;
    *out = malloc(*count * sizeof(EFI_HANDLE));
    (*out)[0] = &pci;
    if (multiple_targets) (*out)[1] = &pci;
    return EFI_SUCCESS;
  }
  *count = 0;
  *out = NULL;
  return EFI_NOT_FOUND;
}
static BOOLEAN same_name(const CHAR16 *a, const CHAR16 *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}
static EFI_STATUS EFIAPI close_file(EFI_FILE_PROTOCOL *file) {
  CHECK(file == &root_dir || file == &marker_dir || file == &run_dir || file == &saved_file);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_file(EFI_FILE_PROTOCOL *dir, EFI_FILE_PROTOCOL **out, CHAR16 *name,
                                   UINT64 mode, UINT64 attributes) {
  *out = NULL;
  if (dir == &root_dir) {
    CHECK(same_name(name, L"\\RTX30-PROBE"));
    CHECK(mode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE) && attributes == 0);
    if (read_only) return EFI_WRITE_PROTECTED;
    if (!marker_present) return EFI_NOT_FOUND;
    *out = &marker_dir;
    return EFI_SUCCESS;
  }
  if (dir == &marker_dir) {
    CHECK(same_name(name, L"RUN00") || (occupied_slot && same_name(name, L"RUN01")));
    if (mode == EFI_FILE_MODE_READ) {
      if (occupied_slot && same_name(name, L"RUN00")) {
        *out = &run_dir;
        return EFI_SUCCESS;
      }
      return EFI_NOT_FOUND;
    }
    CHECK(mode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE));
    CHECK(attributes == EFI_FILE_DIRECTORY);
    CHECK(!occupied_slot || same_name(name, L"RUN01"));
    run_creates++;
    *out = &run_dir;
    return EFI_SUCCESS;
  }
  CHECK(dir == &run_dir && attributes == 0);
  if (same_name(name, L"REPORT.TXT"))
    current_file = 1;
  else if (same_name(name, L"PROM.BIN"))
    current_file = 2;
  else if (same_name(name, L"ROMIMAGE.BIN"))
    current_file = 3;
  else if (same_name(name, L"INVENTORY.TXT"))
    current_file = 4;
  else {
    CHECK(FALSE);
    return EFI_ACCESS_DENIED;
  }
  if (mode == EFI_FILE_MODE_READ) {
    if (current_file == 1 && existing_report) {
      *out = &saved_file;
      return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
  }
  CHECK(mode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE));
  CHECK(!(current_file == 1 && existing_report));
  file_creates++;
  *out = &saved_file;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_file(EFI_FILE_PROTOCOL *file, UINTN *size, VOID *data) {
  CHECK(file == &saved_file && data != NULL && *size > 0);
  saved_size = *size;
  saved_position = 0;
  memcpy(saved_data, data, *size);
  file_writes++;
  if (fail_log) return EFI_VOLUME_FULL;
  if (current_file == 1) {
    CHECK(*size < sizeof(saved_report));
    memcpy(saved_report, data, *size);
    saved_report[*size] = 0;
    report_writes++;
  } else if (current_file == 2) {
    CHECK(*size == PROBE_PROM_SIZE);
    prom_writes++;
  } else if (current_file == 3)
    rom_writes++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI seek_file(EFI_FILE_PROTOCOL *file, UINT64 position) {
  CHECK(file == &saved_file && position <= saved_size);
  saved_position = (UINTN)position;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_file(EFI_FILE_PROTOCOL *file, UINTN *size, VOID *data) {
  CHECK(file == &saved_file);
  if (*size > saved_size - saved_position) *size = saved_size - saved_position;
  memcpy(data, saved_data + saved_position, *size);
  saved_position += *size;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush_file(EFI_FILE_PROTOCOL *file) {
  CHECK(file == &saved_file);
  file_flushes++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs, EFI_FILE_PROTOCOL **out) {
  CHECK(fs == &filesystem);
  fs_opens++;
  *out = &root_dir;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI get_file_info(EFI_FILE_PROTOCOL *file, EFI_GUID *guid, UINTN *size,
                                       VOID *buffer) {
  EFI_GUID info_guid = EFI_FILE_INFO_ID;
  EFI_FILE_INFO *info = buffer;
  CHECK(file == &marker_dir && !memcmp(guid, &info_guid, sizeof(*guid)));
  CHECK(*size >= sizeof(*info));
  memset(info, 0, sizeof(*info));
  info->Size = sizeof(*info);
  info->Attribute = marker_is_file ? 0 : EFI_FILE_DIRECTORY;
  *size = sizeof(*info);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, VOID **out) {
  if (handle == &pci && !memcmp(guid, &pci_guid, sizeof(pci_guid))) {
    *out = &pci;
    return EFI_SUCCESS;
  }
  if (usb_output && handle == (EFI_HANDLE)1 && !memcmp(guid, &loaded_guid, sizeof(*guid))) {
    *out = &loaded_image;
    return EFI_SUCCESS;
  }
  if (usb_output && handle == (EFI_HANDLE)2 && !memcmp(guid, &path_guid, sizeof(*guid))) {
    *out = device_path;
    return EFI_SUCCESS;
  }
  if (usb_output && handle == (EFI_HANDLE)2 && !memcmp(guid, &fs_guid, sizeof(*guid))) {
    *out = &filesystem;
    return EFI_SUCCESS;
  }
  *out = NULL;
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI location(EFI_PCI_IO_PROTOCOL *p, UINTN *seg, UINTN *bus, UINTN *dev,
                                  UINTN *fn) {
  CHECK(p == &pci);
  *seg = 0;
  *bus = 1;
  *dev = 0;
  *fn = 0;
  return EFI_SUCCESS;
}
static void reset(void) {
  memset(config, 0, sizeof(config));
  memset(&pci, 0, sizeof(pci));
  memset(&bs, 0, sizeof(bs));
  memset(&result, 0, sizeof(result));
  config[0] = 0x220610de;
  config[1] = 6;
  config[2] = 0x030000a1;
  config[11] = 0x38901462;
  chip_value = 0xb72000a1;
  bar_size = 0x01000000;
  failed_offset = 0xffffffff;
  config_status = bar_status = EFI_SUCCESS;
  reads = writes = prom_reads = 0;
  pci.Pci.Read = read_config;
  pci.Pci.Write = write_config;
  pci.Mem.Read = read_mem;
  pci.Mem.Write = write_mem;
  pci.GetBarAttributes = get_bar;
  bs.FreePool = free_pool;
  pci.GetLocation = location;
  bs.HandleProtocol = handle_protocol;
  bs.LocateHandleBuffer = locate_handles;
  bs.AllocatePool = allocate;
  bs.Stall = stall;
  memset(&system_table, 0, sizeof(system_table));
  memset(&console_out, 0, sizeof(console_out));
  console_out.OutputString = output_text;
  system_table.ConOut = &console_out;
  system_table.BootServices = &bs;
  console_calls = stall_calls = 0;
  enumerate_target = FALSE;
  multiple_targets = sentinel = fail_log = FALSE;
  memset(&loaded_image, 0, sizeof(loaded_image));
  loaded_image.DeviceHandle = (EFI_HANDLE)2;
  memset(&filesystem, 0, sizeof(filesystem));
  filesystem.OpenVolume = open_volume;
  memset(&root_dir, 0, sizeof(root_dir));
  root_dir.Open = open_file;
  root_dir.Close = close_file;
  marker_dir = run_dir = saved_file = root_dir;
  marker_dir.GetInfo = get_file_info;
  saved_file.Read = read_file;
  saved_file.SetPosition = seek_file;
  saved_file.Write = write_file;
  saved_file.Flush = flush_file;
  device_path[0] = MESSAGING_DEVICE_PATH;
  device_path[1] = MSG_USB_DP;
  device_path[2] = 6;
  device_path[3] = 0;
  device_path[4] = device_path[5] = 0;
  device_path[6] = END_DEVICE_PATH_TYPE;
  device_path[7] = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  device_path[8] = 4;
  device_path[9] = 0;
  usb_output = read_only = occupied_slot = existing_report = FALSE;
  marker_present = TRUE;
  marker_is_file = FALSE;
  fs_opens = run_creates = file_creates = file_writes = file_flushes = 0;
  current_file = report_writes = prom_writes = rom_writes = 0;
  saved_report[0] = 0;
}
int main(void) {
  UINT32 buf[1024];
  unsigned before;
  reset();
  CHECK(ProbeMatches(config));
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS);
  CHECK(result.Config[11] == 0x38901462 && result.BarSize == 0x1000000);
  CHECK(reads == 16 && writes == 0);
  CHECK(result.Samples[0].Value == 0xb72000a1);
  CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_SUCCESS);
  CHECK(buf[0] == 0x0000aa55 && buf[1023] == 0x44332211 && prom_reads == 4);
  CHECK(ProbeRomUsable(buf, sizeof(buf)));
  CHECK(!ProbeRomUsable(NULL, 1024));
  CHECK(!ProbeRomUsable(buf, 1));
  CHECK(!ProbeRomUsable(buf, PROBE_ROM_LIMIT + 1ULL));
  buf[0] = 0xffffffff;
  CHECK(!ProbeRomUsable(buf, sizeof(buf)));

  reset();
  config[0] = 0x268410de;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_UNSUPPORTED && reads == 0);
  reset();
  config[11] = 0x12341462;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS && reads == 16);
  reset();
  config[2] = 0x04000000;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_UNSUPPORTED && reads == 0);

  reset();
  config[1] = 0;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS);
  CHECK(!result.MemoryEnabled && reads == 0 && writes == 0);
  CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_NOT_READY);
  CHECK(config[1] == 0);

  reset();
  config_status = EFI_DEVICE_ERROR;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_DEVICE_ERROR && reads == 0);
  reset();
  bar_status = EFI_UNSUPPORTED;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS && reads == 0);
  CHECK(result.BarStatus == EFI_UNSUPPORTED);
  reset();
  bar_size = 0x100;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS && reads == 1);
  CHECK(result.Samples[1].Status == EFI_BAD_BUFFER_SIZE);
  before = reads;
  CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_NOT_READY);
  CHECK(reads == before);

  reset();
  failed_offset = 0x118128;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS);
  CHECK(result.Samples[1].Status == EFI_DEVICE_ERROR && reads == 16);
  CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_NOT_READY);
  failed_offset = 0xffffffff;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS);
  failed_offset = PROBE_PROM_OFFSET + 1024;
  before = reads;
  CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_DEVICE_ERROR);
  CHECK(reads == before + 2 && writes == 0);
  CHECK(ProbeReadProm(&pci, &result, buf, 0) == EFI_INVALID_PARAMETER);
  CHECK(ProbeReadProm(&pci, &result, buf, 3) == EFI_INVALID_PARAMETER);
  reset();
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(console_calls > 0 && stall_calls == 1 && reads == 0 && writes == 0);
  reset();
  enumerate_target = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(reads == 0 && writes == 0 && prom_reads == 0);
  reset();
  usb_output = enumerate_target = TRUE;
  buf[0] = 0x0000aa55;
  pci.RomImage = buf;
  pci.RomSize = sizeof(buf);
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(fs_opens == 1 && run_creates == 1 && file_creates == 4);
  CHECK(report_writes == 1 && prom_writes == 1 && rom_writes == 1);
  CHECK(file_writes == 4 && file_flushes == 4 && writes == 0 && prom_reads == 1024);
  CHECK(strstr(saved_report, "matching_cards=0x0000000000000001") != NULL);
  /* Feed the real emitted report to the host profile regression tests. */
  {
    FILE *capture = fopen("build/probe-test-report.txt", "wb");
    CHECK(capture != NULL);
    if (capture) {
      fputs(saved_report, capture);
      fclose(capture);
    }
  }
  reset();
  usb_output = TRUE;
  occupied_slot = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(run_creates == 1 && report_writes == 1);
  CHECK(strstr(saved_report, "run_slot=0x0000000000000001") != NULL);
  reset();
  usb_output = TRUE;
  existing_report = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(file_creates == 1 && file_writes == 1);
  reset();
  usb_output = enumerate_target = TRUE;
  read_only = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(fs_opens == 1 && run_creates == 0 && file_writes == 0 && prom_reads == 0);
  reset();
  usb_output = TRUE;
  marker_present = FALSE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(run_creates == 0 && file_writes == 0);
  reset();
  usb_output = marker_is_file = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(run_creates == 0 && file_writes == 0);
  reset();
  usb_output = TRUE;
  device_path[0] = HARDWARE_DEVICE_PATH;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(fs_opens == 0 && file_writes == 0);
  reset();
  usb_output = enumerate_target = multiple_targets = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(reads == 0 && writes == 0 && prom_reads == 0);
  CHECK(strstr(saved_report, "[PCI:0001]") != NULL);
  reset();
  usb_output = enumerate_target = TRUE;
  config[0] = 0x268410de;
  CHECK(ProbeMatches(config) && !ProbeSupportsDetail(config));
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(reads == 0 && writes == 0 && report_writes == 1);
  reset();
  usb_output = enumerate_target = fail_log = TRUE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(reads == 0 && writes == 0 && prom_reads == 0);
  reset();
  sentinel = TRUE;
  CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS && reads == 1);
  CHECK(result.Samples[0].Status == EFI_DEVICE_ERROR);
  CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_NOT_READY);
  {
    static const UINT32 Ids[] = {0x220310de, 0x241410de, 0x248410de, 0x250410de, 0x25a210de};
    static const UINT32 Chips[] = {0xb72000a1, 0xb73000a1, 0xb74000a1, 0xb76000a1, 0xb77000a1};
    for (UINTN i = 0; i < 5; i++) {
      reset();
      config[0] = Ids[i];
      config[11] = 0x12345678;
      chip_value = Chips[i];
      CHECK(ProbeSupportsDetail(config));
      CHECK(GpuGa10xChip(chip_value));
      CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS && reads == 16);
      CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_SUCCESS && writes == 0);
      reset();
      usb_output = enumerate_target = TRUE;
      config[0] = Ids[i];
      chip_value = Chips[i];
      CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
      CHECK(report_writes == 1 && prom_writes == 1 && writes == 0);
    }
    /* A candidate PCI range never authorizes non-GA10x engine/PROM accesses. */
    static const UINT32 Unsupported[] = {0xb70000a1, 0xb92000a1, 0xb62000a1, 0xb75000a1};
    for (UINTN i = 0; i < sizeof(Unsupported) / sizeof(Unsupported[0]); i++) {
      reset();
      config[0] = 0x250410de;
      chip_value = Unsupported[i];
      CHECK(!GpuGa10xChip(chip_value));
      CHECK(ProbeCollect(&pci, &bs, &result) == EFI_SUCCESS && reads == 1);
      CHECK(ProbeReadProm(&pci, &result, buf, sizeof(buf)) == EFI_NOT_READY && reads == 1 &&
            writes == 0);
    }
  }
  reset();
  usb_output = TRUE;
  device_path[7] = END_INSTANCE_DEVICE_PATH_SUBTYPE;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(fs_opens == 0 && file_writes == 0);
  reset();
  usb_output = TRUE;
  device_path[8] = 3;
  CHECK(efi_main((EFI_HANDLE)1, &system_table) == EFI_SUCCESS);
  CHECK(fs_opens == 0 && file_writes == 0);
  printf("Core tests: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
