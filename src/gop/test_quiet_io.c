/* Real unlock file IO with fake firmware; no filesystem or GPU access. */
#include "../unlock/app_io.c"
static EFI_GUID PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
GU_PLATFORM Gu;
GU_ASSET GuAssets[5];
VOID *GuAlloc(size_t n) { return malloc(n); }
VOID GuFree(VOID *p) { free(p); }
BOOLEAN GuSafeToReturn(VOID) { return !Gu.fault; }
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE System;
static EFI_LOADED_IMAGE_PROTOCOL Loaded;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_FILE_PROTOCOL DiskRoot, AssetParent, FileRun, FileReport;
static struct {
  USB_DEVICE_PATH usb;
  EFI_DEVICE_PATH_PROTOCOL end;
} Path;
static unsigned Creates, Writes, Flushes, RunQueries;
static BOOLEAN ShortWrite, PartitionPresent;
static BOOLEAN SharedRunExists, UnlockExists, RunIsFile;
static UINTN RequestedSlot;
static EFI_PARTITION_INFO_PROTOCOL Partition;
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *g, VOID **p) {
  (void)h;
  EFI_GUID partGuid = EFI_PARTITION_INFO_PROTOCOL_GUID;
  if (!memcmp(g, &partGuid, sizeof(*g))) {
    *p = PartitionPresent ? &Partition : NULL;
    return PartitionPresent ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }
  if (!memcmp(g, &LoadedGuid, sizeof(*g)))
    *p = &Loaded;
  else if (!memcmp(g, &PathGuid, sizeof(*g)))
    *p = &Path;
  else {
    assert(!memcmp(g, &FsGuid, sizeof(*g)));
    *p = &Fs;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *f, EFI_FILE_PROTOCOL **p) {
  assert(f == &Fs);
  *p = &DiskRoot;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_FILE_PROTOCOL *f) {
  (void)f;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open(EFI_FILE_PROTOCOL *f, EFI_FILE_PROTOCOL **p, CHAR16 *name,
                              UINT64 mode, UINT64 attr) {
  (void)attr;
  *p = NULL;
  if (mode & EFI_FILE_MODE_CREATE) Creates++;
  if (f == &DiskRoot) {
    static const CHAR16 expected[] = L"\\RTX-UNLOCK";
    assert(!memcmp(name, expected, sizeof(expected)));
    *p = &AssetParent;
    return EFI_SUCCESS;
  }
  if (f == &AssetParent) {
    if (name[0] == 'A') return EFI_NOT_FOUND;
    RunQueries++;
    RequestedSlot = (name[3] - '0') * 10 + name[4] - '0';
    if (mode == EFI_FILE_MODE_READ || !(mode & EFI_FILE_MODE_CREATE)) {
      *p = SharedRunExists ? &FileRun : NULL;
      return *p ? EFI_SUCCESS : EFI_NOT_FOUND;
    }
    *p = &FileRun;
    return EFI_SUCCESS;
  }
  static const CHAR16 expected[] = L"UNLOCK.TXT";
  assert(f == &FileRun && !memcmp(name, expected, sizeof(expected)));
  if (mode == EFI_FILE_MODE_READ) {
    *p = UnlockExists ? &FileReport : NULL;
    return *p ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  *p = &FileReport;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Info(EFI_FILE_PROTOCOL *f, EFI_GUID *g, UINTN *n, VOID *out) {
  (void)g;
  memset(out, 0, *n);
  ((EFI_FILE_INFO *)out)->Size = sizeof(EFI_FILE_INFO);
  ((EFI_FILE_INFO *)out)->Attribute = (f == &FileRun && RunIsFile) ? 0 : EFI_FILE_DIRECTORY;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Write(EFI_FILE_PROTOCOL *f, UINTN *n, VOID *p) {
  assert(f == &FileReport && p && *n);
  Writes++;
  if (ShortWrite) (*n)--;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush(EFI_FILE_PROTOCOL *f) {
  assert(f == &FileReport);
  Flushes++;
  return EFI_SUCCESS;
}
static void Setup(void) {
  memset(&Gu, 0, sizeof(Gu));
  memset(&Bs, 0, sizeof(Bs));
  memset(&System, 0, sizeof(System));
  Bs.HandleProtocol = Handle;
  System.BootServices = &Bs;
  Loaded.DeviceHandle = (EFI_HANDLE)(UINTN)1;
  Fs.OpenVolume = Volume;
  DiskRoot.Open = AssetParent.Open = FileRun.Open = Open;
  AssetParent.GetInfo = FileRun.GetInfo = Info;
  DiskRoot.Close = AssetParent.Close = FileRun.Close = FileReport.Close = Close;
  FileReport.Write = Write;
  FileReport.Flush = Flush;
  memset(&Path, 0, sizeof(Path));
  Path.usb.Header.Type = MESSAGING_DEVICE_PATH;
  Path.usb.Header.SubType = MSG_USB_DP;
  Path.usb.Header.Length[0] = sizeof(Path.usb);
  Path.end.Type = END_DEVICE_PATH_TYPE;
  Path.end.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  Path.end.Length[0] = 4;
  Creates = Writes = Flushes = RunQueries = 0;
  ShortWrite = PartitionPresent = FALSE;
  SharedRunExists = UnlockExists = RunIsFile = FALSE;
  RequestedSlot = 100;
  memset(&Partition, 0, sizeof(Partition));
  Partition.Revision = EFI_PARTITION_INFO_PROTOCOL_REVISION;
  Partition.Type = PARTITION_TYPE_GPT;
  EFI_GUID esp = EFI_PART_TYPE_EFI_SYSTEM_PART_GUID;
  Partition.Info.Gpt.PartitionTypeGUID = esp;
}
int main(void) {
  Setup();
  GuIoSetQuiet(TRUE);
  assert(GuIoOpen(NULL, &System) == EFI_SUCCESS && GuLogReady() && !Report && !RunQueries &&
         !Creates);
  GuLog("ignored %u\n", 1);
  GuLogText("ignored\n");
  assert(!Writes && !Flushes);
  assert(GuLoadAssets() == EFI_NOT_FOUND && !Creates);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  assert(GuIoOpen(NULL, &System) == EFI_SUCCESS && Report && Creates == 2 && Writes == 2 &&
         Flushes == 2);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  ShortWrite = TRUE;
  assert(GuIoOpen(NULL, &System) == EFI_DEVICE_ERROR && !GuLogReady());
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  SharedRunExists = TRUE;
  assert(GuIoOpenRun(NULL, &System, 7) == EFI_SUCCESS && RequestedSlot == 7 && RunQueries == 1 &&
         Creates == 1 && Writes == 2);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  assert(GuIoOpenRun(NULL, &System, 7) == EFI_NOT_FOUND && RequestedSlot == 7 && !Creates &&
         !Writes);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  SharedRunExists = UnlockExists = TRUE;
  assert(GuIoOpenRun(NULL, &System, 7) == EFI_ALREADY_STARTED && !Creates && !Writes);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  SharedRunExists = RunIsFile = TRUE;
  assert(GuIoOpenRun(NULL, &System, 7) == EFI_COMPROMISED_DATA && !Creates && !Writes);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(FALSE);
  assert(GuIoOpenRun(NULL, &System, 100) == EFI_INVALID_PARAMETER && !RunQueries && !Creates);
  Setup();
  GuIoSetQuiet(TRUE);
  assert(GuIoOpenRun(NULL, &System, 100) == EFI_SUCCESS && !RunQueries && !Creates && !Writes);
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(TRUE);
  assert(GuIoOpen(NULL, &System) == EFI_SUCCESS);
  Gu.fault = TRUE;
  assert(GuIoClose() == EFI_ACCESS_DENIED);
  Gu.fault = FALSE;
  assert(GuIoClose() == EFI_SUCCESS);
  Setup();
  GuIoSetQuiet(TRUE);
  Path.usb.Header.SubType = MSG_NVME_NAMESPACE_DP;
  PartitionPresent = TRUE;
#ifdef GU_SSD_EXPERIMENT
  assert(GuIoOpen(NULL, &System) == EFI_SUCCESS && !Creates && !Writes);
  assert(GuIoClose() == EFI_SUCCESS);
  Partition.Info.Gpt.PartitionTypeGUID.Data1 ^= 1;
  assert(GuIoOpen(NULL, &System) == EFI_UNSUPPORTED);
  Setup();
  Path.usb.Header.SubType = MSG_NVME_NAMESPACE_DP;
  PartitionPresent = FALSE;
  assert(GuIoOpen(NULL, &System) == EFI_UNSUPPORTED);
  Setup();
  Path.usb.Header.SubType = MSG_NVME_NAMESPACE_DP;
  PartitionPresent = TRUE;
  Partition.Type = PARTITION_TYPE_MBR;
  assert(GuIoOpen(NULL, &System) == EFI_UNSUPPORTED);
  Setup();
  Path.usb.Header.SubType = MSG_NVME_NAMESPACE_DP;
  PartitionPresent = TRUE;
  Partition.Revision = 0;
  assert(GuIoOpen(NULL, &System) == EFI_UNSUPPORTED);
#else
  assert(GuIoOpen(NULL, &System) == EFI_UNSUPPORTED);
#endif
  puts("PASS unified IO: selected RUN reused without mkdir; existing UNLOCK preserved; quiet no "
       "RUN/create/write; asset/containment failures preserved");
  return 0;
}
