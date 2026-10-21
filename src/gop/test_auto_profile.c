/* C encoder must reproduce Python output for every supported family. */
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include "../probe/probe_profile.h"
static UINT32 Get32(const UINT8 *p) {
  return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}
int main(int argc, char **argv) {
  static const UINT32 offsets[] = {0,        0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828,
                                   0x823804, 0x82381c, 0x823820, 0x1183a4, 0x625f04, 0x1fa7c8,
                                   0x1fa82c, 0x1fa830, 0x824148, 0x8241e0};
  static const UINTN layout[] = {10, 11, 12, 13, 4, 5};
  assert(argc == 6);
  for (int a = 1; a < argc; a++) {
    UINT8 expected[176], actual[176];
    FILE *f = fopen(argv[a], "rb");
    assert(f);
    assert(fread(expected, 1, 176, f) == 176);
    fclose(f);
    PROBE_RESULT r = {0};
    r.Config[0] = Get32(expected + 16);
    r.Config[11] = Get32(expected + 20);
    r.Config[2] = Get32(expected + 28);
    r.MemoryEnabled = TRUE;
    r.BarSize = Get32(expected + 48) | ((UINT64)Get32(expected + 52) << 32);
    for (UINTN i = 0; i < 16; i++)
      r.Samples[i].Offset = offsets[i];
    r.Samples[0].Value = Get32(expected + 24);
    r.Samples[1].Value = 1;
    r.Samples[2].Value = 0xff;
    r.Samples[3].Value = 0x10;
    for (UINTN i = 0; i < 4; i++)
      r.Samples[6 + i].Value = Get32(expected + 32 + i * 4);
    for (UINTN i = 0; i < 6; i++)
      r.Samples[layout[i]].Value = Get32(expected + 56 + i * 4);
    assert(ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    assert(!memcmp(expected, actual, 176));
    PROBE_RESULT good = r;
    for (UINTN i = 0; i < 16; i++) {
      r = good;
      r.Samples[i].Status = EFI_DEVICE_ERROR;
      assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
      r = good;
      r.Samples[i].Value = 0xffffffff;
      assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
      r = good;
      r.Samples[i].Offset++;
      assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    }
    const UINTN cold[] = {1, 2, 3, 9, 11};
    for (UINTN i = 0; i < 5; i++) {
      r = good;
      r.Samples[cold[i]].Value = 0;
      assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    }
    r = good;
    r.Samples[5].Value = 16;
    assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    r = good;
    r.MemoryEnabled = FALSE;
    assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    r = good;
    r.BarSize++;
    assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    r = good;
    r.BarBase = MAX_UINT64;
    assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
    r = good;
    r.Samples[0].Value = 0xb92000a1;
    assert(!ProbeEncodeProfile(&r, expected + 80, expected + 112, actual));
  }
  puts("PASS C/Python byte parity for five families and invalid evidence rejection");
  return 0;
}
