#include "probe_profile.h"
#include "../unlock/sha256.h"
#include "gpu_family.h"

static VOID Put32(UINT8 *p, UINT32 value) {
  for (UINTN i = 0; i < 4; i++)
    p[i] = (UINT8)(value >> (i * 8));
}
BOOLEAN ProbeEncodeProfile(const PROBE_RESULT *r, const UINT8 prom[32], const UINT8 report[32],
                           UINT8 out[PROBE_PROFILE_BYTES]) {
  static const UINT32 offsets[PROBE_SAMPLE_COUNT] = {
      0,        0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828, 0x823804, 0x82381c,
      0x823820, 0x1183a4, 0x625f04, 0x1fa7c8, 0x1fa82c, 0x1fa830, 0x824148, 0x8241e0};
  static const UINTN layout[] = {10, 11, 12, 13, 4, 5};
  static const UINT8 magic[8] = {'G', 'U', 'P', 'R', 'O', 'F', '1', '7'};
  if (!r || !prom || !report || !out || !ProbeSupportsDetail(r->Config) ||
      !GpuGa10xChip(r->Samples[0].Value) || !r->MemoryEnabled || r->BarStatus != EFI_SUCCESS ||
      r->BarSize < 0x842000 || (r->BarSize & (r->BarSize - 1)) ||
      r->BarBase > MAX_UINT64 - (r->BarSize - 1))
    return FALSE;
  for (UINTN i = 0; i < PROBE_SAMPLE_COUNT; i++)
    if (r->Samples[i].Offset != offsets[i] || r->Samples[i].Status != EFI_SUCCESS ||
        r->Samples[i].Value == 0xffffffffU || (r->Samples[i].Value >> 16) == 0xbadf)
      return FALSE;
  if (r->Samples[9].Value < 256 || r->Samples[9].Value > 0x100000 || (r->Samples[5].Value >> 4) ||
      !(r->Samples[11].Value & 1) || !(r->Samples[1].Value & 1) ||
      (r->Samples[2].Value & 0xff) != 0xff || !(r->Samples[3].Value & 0x10))
    return FALSE;
  for (UINTN i = 0; i < 8; i++)
    out[i] = magic[i];
  Put32(out + 8, 1);
  Put32(out + 12, PROBE_PROFILE_BYTES);
  Put32(out + 16, r->Config[0]);
  Put32(out + 20, r->Config[11]);
  Put32(out + 24, r->Samples[0].Value);
  Put32(out + 28, r->Config[2]);
  for (UINTN i = 0; i < 4; i++)
    Put32(out + 32 + i * 4, r->Samples[6 + i].Value);
  Put32(out + 48, (UINT32)r->BarSize);
  Put32(out + 52, (UINT32)(r->BarSize >> 32));
  for (UINTN i = 0; i < 6; i++)
    Put32(out + 56 + i * 4, r->Samples[layout[i]].Value);
  for (UINTN i = 0; i < 32; i++) {
    out[80 + i] = prom[i];
    out[112 + i] = report[i];
  }
  GuSha256(out, 144, out + 144);
  return TRUE;
}
