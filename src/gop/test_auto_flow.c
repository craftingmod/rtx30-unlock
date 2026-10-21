#define _CRT_SECURE_NO_WARNINGS
#define SSD_FLOW_MAIN SsdFlowChecks
#include "test_ssd_flow.c"
#include "../probe/probe_profile.h"
#include "../unlock/sha256.h"
/* Exercise real staging/commit logic; only capture and live PROM reads are simulated. */
#define GuProfilePromChanged RealPromChanged
#include "../unlock/profile_probe.c"
#undef GuProfilePromChanged
static UINT8 Bytes[176], Original[176], Fresh[176];
static EFI_FILE_PROTOCOL ProfileFile, ProbeDir;
static UINTN Position;
static BOOLEAN Exists, Corrupt, Changed, CorruptAfterSave;
static unsigned Captures, Saves, ProbeCloses, Occupied, DirectoryCreates, Replacements;
static EFI_STATUS CaptureStatus, SaveStatus, ProbeCloseStatus, CommitStatus, PromStatus;
static EFI_STATUS EFIAPI ReadProfile(EFI_FILE_PROTOCOL *f, UINTN *n, VOID *out) {
  assert(f == &ProfileFile);
  if (*n > 176 - Position) *n = 176 - Position;
  memcpy(out, Bytes + Position, *n);
  if (Corrupt && *n) ((UINT8 *)out)[0] ^= 1;
  Position += *n;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI SeekProfile(EFI_FILE_PROTOCOL *f, UINT64 n) {
  assert(f == &ProfileFile && n <= 176);
  Position = (UINTN)n;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DummyWrite(EFI_FILE_PROTOCOL *f, UINTN *n, VOID *d) {
  (void)f;
  (void)n;
  (void)d;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI InfoProfile(EFI_FILE_PROTOCOL *f, EFI_GUID *g, UINTN *n, VOID *out) {
  (void)g;
  assert(f == &ProfileFile);
  memset(out, 0, *n);
  ((EFI_FILE_INFO *)out)->Size = sizeof(EFI_FILE_INFO);
  ((EFI_FILE_INFO *)out)->FileSize = 176;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI CloseProbe(EFI_FILE_PROTOCOL *f) {
  assert(f == &ProbeDir);
  ProbeCloses++;
  return ProbeCloseStatus;
}
static EFI_STATUS EFIAPI AutoOpen(EFI_FILE_PROTOCOL *d, EFI_FILE_PROTOCOL **out, CHAR16 *name,
                                  UINT64 mode, UINT64 attr) {
  if (name[0] == 'P' && name[1] == 'R' && name[2] == 'O' && name[3] == 'B') {
    unsigned slot = (name[5] - '0') * 10 + name[6] - '0';
    assert(slot < 100 && DebugMarker);
    if (mode == EFI_FILE_MODE_READ) {
      *out = slot < Occupied ? &ProbeDir : NULL;
      return *out ? EFI_SUCCESS : EFI_NOT_FOUND;
    }
    assert(attr == EFI_FILE_DIRECTORY && slot == Occupied);
    DirectoryCreates++;
    *out = &ProbeDir;
    return EFI_SUCCESS;
  }
  if (name[0] == 'P') {
    assert(mode == EFI_FILE_MODE_READ || mode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE));
    *out = Exists ? &ProfileFile : NULL;
    Position = 0;
    return Exists ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  return ParentOpen(d, out, name, mode, attr);
}
EFI_STATUS GuProfilePromChanged(EFI_SYSTEM_TABLE *t, EFI_PCI_IO_PROTOCOL *p, BOOLEAN *changed) {
  assert(t == &System && p == &TestPci);
  *changed = Changed;
  return PromStatus;
}
EFI_STATUS ProbeCaptureProfile(EFI_SYSTEM_TABLE *t, EFI_FILE_PROTOCOL *d, UINT8 out[176]) {
  assert(t == &System && (DebugMarker ? d == &ProbeDir : d == NULL));
  Captures++;
  memcpy(out, Changed ? Fresh : Bytes, 176);
  return CaptureStatus;
}
EFI_STATUS ProbeSaveNew(EFI_FILE_PROTOCOL *d, CHAR16 *name, VOID *data, UINTN size) {
  assert(d == &ParentFile && name[0] == 'P' && !Exists && size == 176);
  Saves++;
  if (EFI_ERROR(SaveStatus)) return SaveStatus;
  memcpy(Bytes, data, 176);
  Exists = TRUE;
  Corrupt = CorruptAfterSave;
  return EFI_SUCCESS;
}
EFI_STATUS ProbeWriteFile(EFI_FILE_PROTOCOL *f, VOID *data, UINTN size) {
  assert(f == &ProfileFile && size == 176 && Polls == 1 && !Runs && !Starts);
  Replacements++;
  if (EFI_ERROR(CommitStatus)) {
    f->Close(f);
    return CommitStatus;
  }
  memcpy(Bytes, data, 176);
  Corrupt = CorruptAfterSave;
  return f->Close(f);
}
static void AutoSetup(void) {
  SetupFlow();
  Skip = FALSE;
  Exists = Corrupt = Changed = CorruptAfterSave = FALSE;
  Captures = Saves = ProbeCloses = Occupied = DirectoryCreates = Replacements = 0;
  CaptureStatus = SaveStatus = ProbeCloseStatus = CommitStatus = PromStatus = EFI_SUCCESS;
  memcpy(Bytes, Original, 176);
  memcpy(Fresh, Original, 176);
  Fresh[80] ^= 1;
  GuSha256(Fresh, 144, Fresh + 144);
  ParentFile.Open = AutoOpen;
  ProfileFile.Read = ReadProfile;
  ProfileFile.GetInfo = InfoProfile;
  ProfileFile.SetPosition = SeekProfile;
  ProfileFile.Write = DummyWrite;
  ProfileFile.Flush = FileClose;
  ProfileFile.Close = FileClose;
  ProbeDir.Close = CloseProbe;
}
static void Run(void) {
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb");
  assert(f);
  assert(fread(Original, 1, 176, f) == 176);
  fclose(f);
  AutoSetup();
  Run();
  assert(Captures == 1 && Saves == 1 && Exists && !Polls && Runs == 1 && Starts == 1 &&
         !DebugMarker && !DirectoryCreates && !Writes);
  AutoSetup();
  DebugMarker = TRUE;
  Run();
  assert(Captures == 1 && DirectoryCreates == 1 && Polls == 1 && Runs == 1);
  AutoSetup();
  DebugMarker = TRUE;
  Key = 0x1b;
  Run();
  assert(Exists && !Runs && !Starts);
  AutoSetup();
  Exists = TRUE;
  Run();
  assert(!Captures && !Saves && !Polls && Runs == 1);
  AutoSetup();
  Exists = Corrupt = TRUE;
  Run();
  assert(!Captures && !Saves && !Runs && !Starts);
  AutoSetup();
  CaptureStatus = EFI_UNSUPPORTED;
  Run();
  assert(!Exists && !Runs && !Starts);
  AutoSetup();
  SaveStatus = EFI_VOLUME_FULL;
  Run();
  assert(Captures == 1 && !Exists && !Runs && !Starts);
  AutoSetup();
  DebugMarker = TRUE;
  ProbeCloseStatus = EFI_DEVICE_ERROR;
  Run();
  assert(!Exists && !Runs && !Starts);
  AutoSetup();
  DebugMarker = TRUE;
  Occupied = 100;
  Run();
  assert(!Captures && !Exists && !Runs && !Starts);
  AutoSetup();
  Occupied = 100;
  Run();
  assert(Captures == 1 && !DirectoryCreates && Runs == 1);
  AutoSetup();
  CorruptAfterSave = TRUE;
  Run();
  assert(Exists && !GuProfile.identity && !Runs && !Starts);
  AutoSetup();
  Exists = Changed = TRUE;
  Run();
  assert(Captures == 1 && !Saves && Polls == 1 && Replacements == 1 && Starts == 1 &&
         !DirectoryCreates && !DebugMarker && !Writes && !memcmp(Bytes, Fresh, 176));
  AutoSetup();
  Exists = Changed = TRUE;
  Key = 0x1b;
  Run();
  assert(Polls == 1 && !Replacements && !Runs && !Starts && !memcmp(Bytes, Original, 176));
  AutoSetup();
  Exists = Changed = TRUE;
  InputStatus = EFI_NOT_READY;
  Run();
  assert(Polls == 300 && !Replacements && !Runs && !Starts && !memcmp(Bytes, Original, 176));
  AutoSetup();
  Exists = Changed = TRUE;
  CommitStatus = EFI_VOLUME_FULL;
  Run();
  assert(Replacements == 1 && !Runs && !Starts);
  AutoSetup();
  Exists = Changed = TRUE;
  CaptureStatus = EFI_UNSUPPORTED;
  Run();
  assert(!Polls && !Replacements && !Runs && !Starts);
  AutoSetup();
  Exists = Changed = TRUE;
  PreflightStatus = EFI_UNSUPPORTED;
  Run();
  assert(!Polls && !Replacements && !Runs && !Starts);
  AutoSetup();
  Exists = TRUE;
  PromStatus = EFI_DEVICE_ERROR;
  Run();
  assert(!Captures && !Polls && !Replacements && !Runs && !Starts);
  AutoSetup();
  Exists = Changed = DebugMarker = TRUE;
  Run();
  assert(DirectoryCreates == 1 && Polls == 1 && Replacements == 1 && Starts == 1);
  AutoSetup();
  Exists = Changed = Skip = TRUE;
  Run();
  assert(Polls == 1 && Replacements == 1 && !Runs && Starts == 1);
  AutoSetup();
  Exists = Changed = TRUE;
  CorruptAfterSave = TRUE;
  Run();
  assert(Replacements == 1 && !GuProfile.identity && !Runs && !Starts);
  AutoSetup();
  Exists = Changed = TRUE;
  Fresh[16] ^= 1;
  GuSha256(Fresh, 144, Fresh + 144);
  Run();
  assert(!Polls && !Replacements && !Runs && !Starts);
  puts("PASS 22 quiet/debug profile flows: first generation, PROM change U/Esc/timeout, deferred "
       "replacement and failure stops");
  return 0;
}
