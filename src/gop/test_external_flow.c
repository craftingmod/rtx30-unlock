/* Actual v17 entry/log/profile/gate flow; only GPU and Windows are simulated. */
#define _CRT_SECURE_NO_WARNINGS
#define SSD_FLOW_MAIN SsdFlowChecks
#include "test_ssd_flow.c"
static UINT8 ProfileBytes[GU_RUNTIME_PROFILE_BYTES];
static EFI_FILE_PROTOCOL ProfileFile;
static UINTN ProfilePosition;
static BOOLEAN MissingProfile, CorruptProfile;
static EFI_STATUS EFIAPI ProfileRead(EFI_FILE_PROTOCOL *f, UINTN *size, VOID *out) {
  assert(f == &ProfileFile);
  if (*size > sizeof(ProfileBytes) - ProfilePosition)
    *size = sizeof(ProfileBytes) - ProfilePosition;
  memcpy(out, ProfileBytes + ProfilePosition, *size);
  if (CorruptProfile && *size) ((UINT8 *)out)[0] ^= 1;
  ProfilePosition += *size;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ProfileInfo(EFI_FILE_PROTOCOL *f, EFI_GUID *g, UINTN *size, VOID *out) {
  (void)g;
  assert(f == &ProfileFile);
  memset(out, 0, *size);
  ((EFI_FILE_INFO *)out)->Size = sizeof(EFI_FILE_INFO);
  ((EFI_FILE_INFO *)out)->FileSize = sizeof(ProfileBytes);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ProfileOpen(EFI_FILE_PROTOCOL *d, EFI_FILE_PROTOCOL **out, CHAR16 *name,
                                     UINT64 mode, UINT64 attr) {
  if (name[0] != 'P') return ParentOpen(d, out, name, mode, attr);
  assert(mode == EFI_FILE_MODE_READ && !attr);
  *out = MissingProfile ? NULL : &ProfileFile;
  ProfilePosition = 0;
  return MissingProfile ? EFI_NOT_FOUND : EFI_SUCCESS;
}
static void ExternalSetup(void) {
  SetupFlow();
  Skip = FALSE;
  MissingProfile = CorruptProfile = FALSE;
  ParentFile.Open = ProfileOpen;
  ProfileFile.GetInfo = ProfileInfo;
  ProfileFile.Read = ProfileRead;
  ProfileFile.Close = FileClose;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb");
  assert(f);
  assert(fread(ProfileBytes, 1, sizeof(ProfileBytes), f) == sizeof(ProfileBytes));
  fclose(f);
  ExternalSetup();
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && Runs == 1 && !Polls && !Writes && GuProfile.identity);
  ExternalSetup();
  DebugMarker = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && Runs == 1 && Polls == 1 && strstr(SavedLog, "PROFILE v1 loaded"));
  ExternalSetup();
  DebugMarker = TRUE;
  Key = 0x1b;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Runs && Polls == 1);
  ExternalSetup();
  DebugMarker = TRUE;
  InputStatus = EFI_NOT_READY;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Runs && Polls == 300);
  ExternalSetup();
  MissingProfile = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Plans && !Runs && !Polls && !GuProfile.identity);
  ExternalSetup();
  CorruptProfile = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Plans && !Runs && !Polls && !GuProfile.identity);
  ExternalSetup();
  Skip = TRUE;
  DebugMarker = TRUE;
  Key = 0x1b;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Plans && !Runs && Polls == 1);
  ExternalSetup();
  Skip = TRUE;
  DebugMarker = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && !Plans && !Runs && Polls == 1);
  puts("PASS 8 v17 app flows: external profile required before prepare, quiet auto, debug "
       "U/Esc/timeout including skip");
  return 0;
}
