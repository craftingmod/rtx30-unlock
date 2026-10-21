#define main ProbeFixtureMain
#include "../probe/test_core.c"
#undef main
#include <assert.h>
#include "../probe/probe_profile.h"
#include "../unlock/sha256.h"
#include "../unlock/runtime_profile.h"
static unsigned Fault;
static EFI_STATUS EFIAPI ColdRead(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT8 bar,
                                  UINT64 offset, UINTN count, VOID *out) {
  EFI_STATUS s = read_mem(p, w, bar, offset, count, out);
  if (!EFI_ERROR(s) && offset < PROBE_PROM_OFFSET) {
    if (offset == 0x118128) *(UINT32 *)out = 1;
    if (offset == 0x118234) *(UINT32 *)out = 0xff;
    if (offset == 0x110100) *(UINT32 *)out = 0x10;
    if (offset == 0x1183a4) *(UINT32 *)out = 10240;
    if (offset == 0x1fa7c8) *(UINT32 *)out = 1;
    if (offset == 0x1fa828) *(UINT32 *)out = 0;
  }
  return s;
}
static EFI_STATUS EFIAPI FaultRead(EFI_FILE_PROTOCOL *f, UINTN *size, VOID *out) {
  EFI_STATUS s = read_file(f, size, out);
  if (Fault == 1 && *size) ((UINT8 *)out)[0] ^= 1;
  if (Fault == 2 && *size) (*size)--;
  return s;
}
static EFI_STATUS EFIAPI FaultClose(EFI_FILE_PROTOCOL *f) {
  close_file(f);
  return Fault == 3 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FaultWrite(EFI_FILE_PROTOCOL *f, UINTN *size, VOID *data) {
  EFI_STATUS s = write_file(f, size, data);
  if (Fault == 4 && *size) (*size)--;
  return Fault == 5 ? EFI_VOLUME_FULL : s;
}
static EFI_STATUS EFIAPI FaultFlush(EFI_FILE_PROTOCOL *f) {
  flush_file(f);
  return Fault == 6 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ProfileOpen(EFI_FILE_PROTOCOL *d, EFI_FILE_PROTOCOL **out, CHAR16 *name,
                                     UINT64 mode, UINT64 attr) {
  (void)d;
  assert(name[0] == 'P' && !attr &&
         (mode == EFI_FILE_MODE_READ || mode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE)));
  saved_position = 0;
  *out = &saved_file;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ProfileInfo(EFI_FILE_PROTOCOL *f, EFI_GUID *g, UINTN *size, VOID *out) {
  (void)g;
  assert(f == &saved_file);
  memset(out, 0, *size);
  ((EFI_FILE_INFO *)out)->Size = sizeof(EFI_FILE_INFO);
  ((EFI_FILE_INFO *)out)->FileSize = saved_size;
  return EFI_SUCCESS;
}
static void Setup(void) {
  reset();
  Fault = 0;
  enumerate_target = TRUE;
  pci.Mem.Read = ColdRead;
  saved_file.Read = FaultRead;
  saved_file.Close = FaultClose;
  saved_file.Write = FaultWrite;
  saved_file.Flush = FaultFlush;
}
int main(void) {
  UINT8 bytes[176], hash[32];
  Setup();
  assert(ProbeCaptureProfile(&system_table, &run_dir, bytes) == EFI_SUCCESS);
  assert(!writes && !stall_calls && prom_writes == 1 && report_writes == 1);
  GuSha256(saved_report, strlen(saved_report), hash);
  assert(!memcmp(bytes + 112, hash, 32));
  FILE *f = fopen("build/auto-profile-report.txt", "wb");
  assert(f);
  fwrite(saved_report, 1, strlen(saved_report), f);
  fclose(f);
  f = fopen("build/auto-profile.bin", "wb");
  assert(f);
  fwrite(bytes, 1, 176, f);
  fclose(f);
  static UINT32 prom[PROBE_PROM_SIZE / 4];
  for (UINTN i = 0; i < PROBE_PROM_SIZE / 4; i++)
    prom[i] = 0x44332211;
  prom[0] = 0xaa55;
  f = fopen("build/auto-profile-prom.bin", "wb");
  assert(f);
  fwrite(prom, 1, sizeof(prom), f);
  fclose(f);
  Setup();
  assert(ProbeCaptureProfile(&system_table, NULL, bytes) == EFI_SUCCESS);
  assert(!file_creates && !file_writes && !report_writes && !prom_writes && !rom_writes &&
         !writes && !run_creates);
  assert(GuProfileDecode(bytes, sizeof(bytes), &GuProfile));
  BOOLEAN changed = TRUE;
  assert(GuProfilePromChanged(&system_table, &pci, &changed) == EFI_SUCCESS && !changed);
  GuProfile.prom_sha256[0] = GuProfile.prom_sha256[0] == '0' ? '1' : '0';
  assert(GuProfilePromChanged(&system_table, &pci, &changed) == EFI_SUCCESS && changed);
  failed_offset = PROBE_PROM_OFFSET;
  assert(EFI_ERROR(GuProfilePromChanged(&system_table, &pci, &changed)) && !changed);
  failed_offset = 0xffffffff;
  GuProfile.identity ^= 0x10000;
  assert(GuProfilePromChanged(&system_table, &pci, &changed) == EFI_UNSUPPORTED && !changed);
  Setup();
  assert(ProbeCaptureProfile(&system_table, NULL, bytes) == EFI_SUCCESS);
  UINT8 prior[176];
  memcpy(prior, bytes, 176);
  prior[80] ^= 1;
  GuSha256(prior, 144, prior + 144);
  EFI_FILE_PROTOCOL parent = {0};
  parent.Open = ProfileOpen;
  saved_file.GetInfo = ProfileInfo;
  memcpy(saved_data, prior, 176);
  saved_size = 176;
  assert(GuProfileRefresh(&system_table, &parent, FALSE) == EFI_SUCCESS);
  assert(!memcmp(saved_data, prior, 176) && !file_writes); /* Still unapproved. */
  assert(GuProfileCommit(&parent) == EFI_SUCCESS && file_writes == 1 &&
         !memcmp(saved_data, bytes, 176));
  memcpy(saved_data, prior, 176);
  assert(GuProfileRefresh(&system_table, &parent, FALSE) == EFI_SUCCESS);
  saved_data[80] ^= 1; /* Another writer changed the profile while U was pending. */
  assert(GuProfileCommit(&parent) == EFI_COMPROMISED_DATA && file_writes == 1);
  for (unsigned fault = 3; fault <= 6; fault++) {
    Fault = 0;
    memcpy(saved_data, prior, 176);
    saved_size = 176;
    assert(GuProfileRefresh(&system_table, &parent, FALSE) == EFI_SUCCESS);
    Fault = fault;
    assert(EFI_ERROR(GuProfileCommit(&parent)));
  }
  for (unsigned fault = 1; fault <= 6; fault++) {
    Setup();
    Fault = fault;
    assert(EFI_ERROR(ProbeCaptureProfile(&system_table, &run_dir, bytes)) && !reads && !writes);
  }
  Setup();
  multiple_targets = TRUE;
  assert(EFI_ERROR(ProbeCaptureProfile(&system_table, &run_dir, bytes)) && !reads);
  Setup();
  failed_offset = 0x118128;
  assert(EFI_ERROR(ProbeCaptureProfile(&system_table, &run_dir, bytes)) && !prom_writes);
  Setup();
  fail_log = TRUE;
  assert(EFI_ERROR(ProbeCaptureProfile(&system_table, &run_dir, bytes)) && !reads);
  Setup();
  existing_report = TRUE;
  assert(EFI_ERROR(ProbeCaptureProfile(&system_table, &run_dir, bytes)));
  assert(!failures);
  puts("PASS actual Probe capture, persisted evidence checks and failure refusal");
  return 0;
}
