/* Exercise the real teardown/rejoin with a simulated unlock transaction. */
#define GC_PATCH_FIXTURE
#include "test_full.c"
#include "patch.h"
#include "visual.h"
static GF_PLAN *Plan;
static unsigned PatchCalls, Observations, Released, VisualCalls;
static BOOLEAN PatchSafe, PatchError, LateOwner, LateInput, LoseAfter, LoseBefore, FailPhase,
    DisplayFailure;
static unsigned ChildCase;
static EFI_STATUS EFIAPI ChildOpens(EFI_HANDLE h, EFI_GUID *g,
                                    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out, UINTN *n) {
  EFI_STATUS s = FOpens(h, g, out, n);
  if (!EFI_ERROR(s) && h == ROOT && Flrs && FullStarts && Eq(g, &PathGuid)) {
    if (ChildCase == 1) *n = 0;
    if (ChildCase == 2) *n = 1;
    if (ChildCase == 3) {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *larger = Alloc(3 * sizeof(**out));
      memcpy(larger, *out, *n * sizeof(**out));
      assert(Bs.FreePool(*out) == EFI_SUCCESS);
      *out = larger;
      Add(*out, n, VENDOR, H(99), EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
    }
    if (ChildCase == 4) s = EFI_UNSUPPORTED;
    if (ChildCase == 5 && *n) (*out)[0].AgentHandle = H(99);
    if (ChildCase == 7) *n = 129;
  }
  return s;
}
static EFI_STATUS EFIAPI ChildHandle(EFI_HANDLE h, EFI_GUID *g, VOID **out) {
  if (ChildCase == 6 && Flrs && FullStarts && h == Current(PHYS) && Eq(g, &GopGuid)) {
    *out = NULL;
    return EFI_UNSUPPORTED;
  }
  return FHandle(h, g, out);
}
BOOLEAN GvMark(GC_CONTEXT *c, const char *phase) {
  assert(c == &Context && phase);
  return !FailPhase;
}
EFI_STATUS GvDisplay(GC_CONTEXT *c, const GL_PLAN *old, const GL_PLAN *fresh) {
  assert(c == &Context && old && fresh && Observations == 2 && !Exclusive && Flrs == 1);
  VisualCalls++;
  return DisplayFailure ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
BOOLEAN GxSafe(VOID) { return PatchSafe; }
VOID GxReleased(VOID) { Released++; }
EFI_STATUS GxPatch(BOOLEAN (*isolated)(VOID)) {
  PatchCalls++;
  assert(Exclusive && !FullStarts && isolated());
  if (LateOwner) ResidualOwner = TRUE;
  if (LateInput) GuardChanged = TRUE;
  if (!isolated()) return EFI_ACCESS_DENIED;
  if (PatchError) return EFI_DEVICE_ERROR;
  BOOLEAN reset = FALSE;
  EFI_STATUS s = GfResetPci(&Context, &Plan->pci, &reset);
  assert(!EFI_ERROR(s) && reset && Flrs == 1);
  return s;
}
EFI_STATUS GxObserve(GC_CONTEXT *c, const char *phase) {
  assert(c == &Context && Flrs == 1);
  Observations++;
  if (!strcmp(phase, "before-gop")) {
    assert(Exclusive && !FullStarts);
    return LoseBefore ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
  }
  assert(!strcmp(phase, "after-gop") && !Exclusive && FullStarts && Bits == 3);
  return LoseAfter ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
}
static void Setup(void) {
  FReset();
  PatchCalls = Observations = Released = VisualCalls = 0;
  PatchSafe = TRUE;
  PatchError = LateOwner = LateInput = LoseAfter = LoseBefore = FailPhase = DisplayFailure =
      FailChildLog = FALSE;
  ChildCase = 0;
  Bs.OpenProtocolInformation = ChildOpens;
  Bs.HandleProtocol = ChildHandle;
  FPrepare(Plan);
}
int main(void) {
  Plan = malloc(sizeof(*Plan));
  assert(Plan);
  GC_RESULT r;
  Setup();
  Recreate = TRUE;
  assert(GfRun(&Context, Plan, &r) == EFI_SUCCESS && r.restored && NewHandles);
  assert(PatchCalls == 1 && Observations == 2 && Released == 1 && CloseCount == 1 && Flrs == 1 &&
         VisualCalls == 1);
  assert(strstr(FullLog, "FULL_PATCH_CHECK_BEGIN call=2") &&
         strstr(FullLog, "FULL_PATCH_CHECK_END call=2 status=0"));
  assert(strstr(FullLog, "CHILD_INSPECT_END phase=post-start status=0 unique=2") &&
         strstr(FullLog, "CHILD_PROTOCOL_RESULT phase=post-start"));
  Setup();
  LoseAfter = TRUE;
  assert(GfRun(&Context, Plan, &r) == EFI_COMPROMISED_DATA && r.restored && Observations == 2);
  Setup();
  LoseBefore = TRUE;
  assert(GfRun(&Context, Plan, &r) == EFI_COMPROMISED_DATA && !FullStarts && !CloseCount &&
         Exclusive);
  Setup();
  PatchError = TRUE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && PatchCalls == 1 && !Flrs && !FullStarts &&
         Exclusive);
  Setup();
  PatchSafe = FALSE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && Flrs == 1 && !FullStarts && !CloseCount);
  Setup();
  LateOwner = TRUE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !Flrs && !FullStarts);
  Setup();
  LateInput = TRUE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !Flrs && !FullStarts);
  Setup();
  ExposeGop = TRUE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !PatchCalls && !Flrs);
  Setup();
  ResidualVirtual = TRUE;
  VirtualMode.FrameBufferBase = VirtualMode.FrameBufferSize = 0;
  assert(GfRun(&Context, Plan, &r) == EFI_SUCCESS && r.restored && PatchCalls == 1 &&
         Observations == 2);
  assert(!strstr(FullLog, "FULL_TRACE"));
  Setup();
  FailClose = TRUE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !FullStarts && !Released && Observations == 1);
  Setup();
  FailStartRole = 1;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !r.restored && Observations == 1);
  Setup();
  Config[1] |= 4;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !PatchCalls && !r.touched);
  Setup();
  FailPhase = TRUE;
  assert(EFI_ERROR(GfRun(&Context, Plan, &r)) && !PatchCalls && !Flrs && !FullStarts);
  Setup();
  DisplayFailure = TRUE;
  assert(GfRun(&Context, Plan, &r) == EFI_DEVICE_ERROR && r.restored && VisualCalls == 1 &&
         Flrs == 1);
  for (unsigned count = 0; count < 2; count++) {
    Setup();
    ChildCase = count + 1;
    assert(GfRun(&Context, Plan, &r) == EFI_UNSUPPORTED && FullStarts == 1 && !VisualCalls &&
           !r.restored);
    assert(strstr(FullLog, count ? "unique=1" : "unique=0") &&
           strstr(FullLog, "FULL_CHILDREN_RESULT phase=post-start status=0x8000000000000003"));
  }
  Setup();
  ChildCase = 3;
  assert(GfRun(&Context, Plan, &r) == EFI_UNSUPPORTED && FullStarts == 1 && !VisualCalls);
  assert(strstr(FullLog, "CHILD_INSPECT_END phase=post-start status=0 unique=3"));
  Setup();
  ChildCase = 4;
  assert(GfRun(&Context, Plan, &r) == EFI_UNSUPPORTED && FullStarts == 1 && !VisualCalls);
  assert(strstr(FullLog, "protocol=device-path status=0x8000000000000003"));
  Setup();
  ChildCase = 5;
  assert(GfRun(&Context, Plan, &r) == EFI_ACCESS_DENIED && FullStarts == 1 && !VisualCalls);
  assert(strstr(FullLog, "expected_agent=0 child_edge=1"));
  Setup();
  ChildCase = 6;
  assert(GfRun(&Context, Plan, &r) == EFI_NOT_FOUND && FullStarts == 1 && !VisualCalls);
  assert(strstr(FullLog, "FULL_CHILD_GOP candidate=0") &&
         strstr(FullLog, "FULL_CHILD_GOP candidate=1"));
  Setup();
  ChildCase = 7;
  assert(GfRun(&Context, Plan, &r) == EFI_COMPROMISED_DATA && FullStarts == 1 && !VisualCalls);
  Setup();
  FailChildLog = TRUE;
  assert(GfRun(&Context, Plan, &r) == EFI_DEVICE_ERROR && FullStarts == 1 && !VisualCalls);
  assert(!Allocations);
  free(Plan);
  puts("PASS 22 unlock/rejoin/child-inspection scenarios (unlock firmware simulated)");
  return 0;
}
