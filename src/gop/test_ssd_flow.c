/* Actual EFI app flow; GPU transaction and Windows execution are simulated. */
#define main IoFixtureMain
#include "test_io.c"
#undef main
#include <setjmp.h>
static jmp_buf ExitFlow;
static unsigned Plans, Runs, Finishes, Starts;
static BOOLEAN Skip;
static EFI_STATUS PreflightStatus, RunStatus;
EFI_STATUS GxPrepare(GC_CONTEXT *c) {
  assert(c->run_slot == (c->quiet ? 100 : OccupiedRuns));
  return PreflightStatus;
}
BOOLEAN GxAlreadyApplied(VOID) { return Skip; }
BOOLEAN GxSafe(VOID) { return TRUE; }
EFI_STATUS GxFinish(VOID) {
  Finishes++;
  return EFI_SUCCESS;
}
EFI_STATUS GxObserve(GC_CONTEXT *c, const char *p) {
  (void)c;
  (void)p;
  return EFI_SUCCESS;
}
BOOLEAN GvMark(GC_CONTEXT *c, const char *p) { return c->log("TIME phase=%s\n", p); }
EFI_STATUS GfPrepare(GC_CONTEXT *c, GF_PLAN *p) {
  (void)c;
  (void)p;
  Plans++;
  return EFI_SUCCESS;
}
EFI_STATUS GfRun(GC_CONTEXT *c, const GF_PLAN *p, GC_RESULT *r) {
  (void)c;
  (void)p;
  Runs++;
  r->touched = r->restored = TRUE;
  return RunStatus;
}
EFI_STATUS GwPrepare(GC_CONTEXT *c, GW_PLAN *p) {
  (void)c;
  p->image = (EFI_HANDLE)(UINTN)99;
  return EFI_SUCCESS;
}
EFI_STATUS GwCancel(GC_CONTEXT *c, GW_PLAN *p) {
  (void)c;
  (void)p;
  return EFI_SUCCESS;
}
EFI_STATUS GwStart(GC_CONTEXT *c, GW_PLAN *p, EFI_FILE_PROTOCOL **f, BOOLEAN verified) {
  assert(p->image && verified && (c->quiet ? !*f : *f != NULL));
  Starts++;
  longjmp(ExitFlow, 1);
  return EFI_ABORTED;
}
static EFI_STATUS EFIAPI FlowStall(UINTN n) {
  if (n == 1000000) longjmp(ExitFlow, 2);
  return EFI_SUCCESS;
}
static void SetupFlow(void) {
  Startup();
  DebugMarker = FALSE;
  SecureEnabled = 0;
  Services.Stall = FlowStall;
  Plans = Runs = Finishes = Starts = 0;
  PreflightStatus = RunStatus = EFI_SUCCESS;
}
#ifndef SSD_FLOW_MAIN
#define SSD_FLOW_MAIN main
#endif
int SSD_FLOW_MAIN(void) {
  SetupFlow();
  Skip = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && !Plans && !Runs && Finishes == 1 && !Writes && !Polls);
  SetupFlow();
  Skip = FALSE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && Plans == 1 && Runs == 1 && Finishes == 1 && !Writes && !Polls);
  SetupFlow();
  Skip = FALSE;
  PreflightStatus = EFI_NOT_READY;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Plans && !Runs && Finishes == 1);
  SetupFlow();
  Skip = FALSE;
  RunStatus = EFI_DEVICE_ERROR;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && Plans == 1 && Runs == 1);
  SetupFlow();
  Skip = FALSE;
  DebugMarker = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && Plans == 1 && Runs == 1 && Polls == 1 && strstr(SavedLog, "MANUAL_U"));
  SetupFlow();
  Skip = FALSE;
  DebugMarker = TRUE;
  Key = 0x1b;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Runs && Polls == 1);
  SetupFlow();
  Skip = FALSE;
  DebugMarker = TRUE;
  InputStatus = EFI_NOT_READY;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Runs && Polls == 300);
  SetupFlow();
  Skip = TRUE;
  DebugMarker = TRUE;
  Key = 0x1b;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(!Starts && !Plans && !Runs && Polls == 1);
  SetupFlow();
  Skip = TRUE;
  DebugMarker = TRUE;
  if (!setjmp(ExitFlow)) efi_main((EFI_HANDLE)(UINTN)77, &System);
  assert(Starts == 1 && !Plans && !Runs && Polls == 1);
  puts("PASS 9 actual SSD flows: auto/debug U, Esc/timeout stop apply and skip, "
       "preflight/transaction failures block Windows");
  return 0;
}
