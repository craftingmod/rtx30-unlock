#pragma once
#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>

/* v17 little-endian wire format: 144-byte payload followed by SHA256. */
#define GU_RUNTIME_PROFILE_BYTES 176U
typedef struct {
  UINT32 identity, subsystem, chip, class_revision;
  UINT32 plm, ss0, ss1, fb_mib;
  UINT64 bar_size;
  UINT32 layout[6];
  char prom_sha256[65];
} GU_RUNTIME_PROFILE;
extern GU_RUNTIME_PROFILE GuProfile;
BOOLEAN GuProfileDecode(const UINT8 *data, UINTN size, GU_RUNTIME_PROFILE *out);
EFI_STATUS GuProfileRead(EFI_FILE_PROTOCOL *directory, UINT8 *copy);
EFI_STATUS GuProfileLoad(EFI_FILE_PROTOCOL *directory);

#ifdef GU_RUNTIME_PROFILE_BUILD
#define GU_PROFILE_BUILD 1
#define GU_BOARD_ID (GuProfile.identity)
#define GU_BOARD_SUBSYSTEM (GuProfile.subsystem)
#define GU_BOARD_CHIP (GuProfile.chip)
#define GU_CLASS_REVISION (GuProfile.class_revision)
#define GU_INITIAL_PLM (GuProfile.plm)
#define GU_INITIAL_SS0 (GuProfile.ss0)
#define GU_INITIAL_SS1 (GuProfile.ss1)
#define GU_FB_MIB (GuProfile.fb_mib)
#define GU_BAR_SIZE (GuProfile.bar_size)
#define GU_PROM_SHA256 (GuProfile.prom_sha256)
#define GU_TARGET_PCI_ID GU_BOARD_ID
#define GU_LAYOUT_OFFSETS {0x625f04U, 0x1fa7c8U, 0x1fa82cU, 0x1fa830U, 0x1fa824U, 0x1fa828U}
#define GU_LAYOUT_VALUES                                                                           \
  {GuProfile.layout[0], GuProfile.layout[1], GuProfile.layout[2],                                  \
   GuProfile.layout[3], GuProfile.layout[4], GuProfile.layout[5]}
#endif
