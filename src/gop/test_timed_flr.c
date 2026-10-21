#define main PortRegressionMain
#include "../unlock/tests/test_port.c"
#undef main
static BOOLEAN FailBegin, FailEnd, MasterRace;
static unsigned BeginCalls, EndCalls;
BOOLEAN GuPhase(const char *phase) {
  assert(!fake.flr_seen || fake.delay >= 100000);
  if (!strcmp(phase, "flr-begin")) {
    BeginCalls++;
    if (MasterRace) config[4] |= 4;
    if (FailBegin) {
      Gu.fault = TRUE;
      return FALSE;
    }
  } else {
    assert(!strcmp(phase, "flr-end") && fake.flr_seen && fake.delay >= 100000);
    EndCalls++;
    if (FailEnd) {
      Gu.fault = TRUE;
      return FALSE;
    }
  }
  return TRUE;
}
int main(void) {
  assert(PortRegressionMain() == 0);
  setup();
  BeginCalls = EndCalls = 0;
  assert(GuSavePci() == EFI_SUCCESS && GuFlr() == EFI_SUCCESS);
  assert(BeginCalls == 1 && EndCalls == 1 && !fake.config_too_early && Gu.flrs == 1);
  setup();
  FailBegin = TRUE;
  assert(GuSavePci() == EFI_SUCCESS && EFI_ERROR(GuFlr()) && !fake.flr_seen);
  FailBegin = FALSE;
  setup();
  MasterRace = TRUE;
  assert(GuSavePci() == EFI_SUCCESS && EFI_ERROR(GuFlr()) && !fake.flr_seen);
  MasterRace = FALSE;
  setup();
  FailEnd = TRUE;
  assert(GuSavePci() == EFI_SUCCESS && EFI_ERROR(GuFlr()) && fake.delay >= 100000 && Gu.flrs == 1);
  puts("PASS 4 timed FLR scenarios; callback precedes final BME checks and 100ms quiet interval is "
       "preserved");
  return 0;
}
