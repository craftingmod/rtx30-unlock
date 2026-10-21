/* Same executable consumes profiles from all five families; no GPU access. */
#define _CRT_SECURE_NO_WARNINGS
#define main ProfileChecks
#include "test_profile.c"
#undef main
#include "../runtime_profile.h"
#include "../sha256.h"
#include <Guid/FileInfo.h>

static UINT8 Bytes[GU_RUNTIME_PROFILE_BYTES + 1];
static UINTN Position;
static unsigned Mode, Closes;
static EFI_FILE_PROTOCOL Directory, ProfileFile;
static EFI_STATUS EFIAPI OpenProfile(EFI_FILE_PROTOCOL *d, EFI_FILE_PROTOCOL **out, CHAR16 *name,
                                     UINT64 mode, UINT64 attr) {
  assert(d == &Directory && name[0] == 'P' && mode == EFI_FILE_MODE_READ && !attr);
  *out = Mode == 1 ? NULL : &ProfileFile;
  Position = 0;
  return Mode == 1 ? EFI_NOT_FOUND : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI CloseProfile(EFI_FILE_PROTOCOL *f) {
  assert(f == &ProfileFile);
  Closes++;
  return Mode == 7 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI InfoProfile(EFI_FILE_PROTOCOL *f, EFI_GUID *g, UINTN *size, VOID *out) {
  (void)g;
  assert(f == &ProfileFile);
  memset(out, 0, *size);
  EFI_FILE_INFO *info = out;
  info->Size = sizeof(*info);
  info->FileSize = GU_RUNTIME_PROFILE_BYTES;
  if (Mode == 2) info->FileSize--;
  if (Mode == 3) info->Attribute = EFI_FILE_DIRECTORY;
  if (Mode == 4) *size = 1;
  return Mode == 5 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReadProfile(EFI_FILE_PROTOCOL *f, UINTN *size, VOID *out) {
  assert(f == &ProfileFile);
  UINTN end = GU_RUNTIME_PROFILE_BYTES + (Mode == 8);
  if (*size > end - Position) *size = end - Position;
  if (Mode == 6 && *size) (*size)--;
  memcpy(out, Bytes + Position, *size);
  Position += *size;
  return Mode == 10 ? EFI_NOT_FOUND : Mode == 9 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static void Put32(UINTN offset, UINT32 value) {
  for (unsigned i = 0; i < 4; i++)
    Bytes[offset + i] = (UINT8)(value >> (i * 8));
}
static void Rehash(void) { GuSha256(Bytes, 144, Bytes + 144); }
int main(int argc, char **argv) {
  assert(argc > 1);
  Directory.Open = OpenProfile;
  ProfileFile.GetInfo = InfoProfile;
  ProfileFile.Read = ReadProfile;
  ProfileFile.Close = CloseProfile;
  for (int arg = 1; arg < argc; arg++) {
    FILE *f = fopen(argv[arg], "rb");
    assert(f);
    assert(fread(Bytes, 1, sizeof(Bytes), f) == GU_RUNTIME_PROFILE_BYTES);
    fclose(f);
    GU_RUNTIME_PROFILE parsed = {0};
    assert(GuProfileDecode(Bytes, GU_RUNTIME_PROFILE_BYTES, &parsed));
    UINT8 original[GU_RUNTIME_PROFILE_BYTES];
    memcpy(original, Bytes, sizeof(original));
    for (UINTN i = 0; i < GU_RUNTIME_PROFILE_BYTES; i++) {
      Bytes[i] ^= 1;
      assert(!GuProfileDecode(Bytes, GU_RUNTIME_PROFILE_BYTES, &parsed));
      Bytes[i] ^= 1;
      assert(!GuProfileDecode(Bytes, i, &parsed));
    }
    assert(!GuProfileDecode(Bytes, sizeof(Bytes), &parsed));
    assert(!GuProfileDecode(NULL, GU_RUNTIME_PROFILE_BYTES, &parsed));
    const UINTN offsets[] = {0, 8, 12, 16, 24, 28, 44, 48, 60, 76};
    const UINT32 invalid[] = {0, 2, 0, 0x268410de, 0xb92000a1, 0x020000a1, 0, 0x842001, 0, 0x10};
    for (UINTN i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
      Put32(offsets[i], invalid[i]);
      Rehash();
      assert(!GuProfileDecode(Bytes, GU_RUNTIME_PROFILE_BYTES, &parsed));
      memcpy(Bytes, original, sizeof(original));
    }
    Mode = Closes = 0;
    assert(GuProfileLoad(&Directory) == EFI_SUCCESS && Closes == 1);
    assert(GuProfile.identity == parsed.identity);
    same_bdf = read_error = wrong_class = 0;
    assert(ProfileChecks() == 0);
    for (Mode = 1; Mode <= 10; Mode++) {
      Closes = 0;
      assert(EFI_ERROR(GuProfileLoad(&Directory)) && !GuProfile.identity);
      assert(Closes == (Mode == 1 ? 0U : 1U));
      if (Mode == 10) assert(GuProfileLoad(&Directory) != EFI_NOT_FOUND);
    }
    Mode = 0;
    Bytes[80] ^= 1;
    assert(GuProfileLoad(&Directory) == EFI_COMPROMISED_DATA && !GuProfile.identity);
  }
  puts("PASS external profile decode/integrity/semantic checks, bounded file IO and live target "
       "guards for all supplied profiles");
  return 0;
}
