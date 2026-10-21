/* Actual discovery/load/start code; no disks, NVRAM or firmware execution. */
#include "windows.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE Table;
static GC_CONTEXT Context;
static GW_PLAN Plan;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_FILE_PROTOCOL Root, File, Report;
static EFI_LOADED_IMAGE_PROTOCOL Loaded;
static struct {
  USB_DEVICE_PATH usb;
  HARDDRIVE_DEVICE_PATH hd;
  EFI_DEVICE_PATH_PROTOCOL end;
} Path;
static unsigned Count, Present, Loads, Starts, Unloads, Allocations, Closes;
static BOOLEAN Usb, Malformed, MissingImage, BadLoaded, FailLog, FailClose, Directory, Empty;
static EFI_STATUS LoadStatus, StartStatus, UnloadStatus;
static BOOLEAN Log(const char *fmt, ...) {
  (void)fmt;
  return !FailLog;
}
static EFI_STATUS EFIAPI Handles(EFI_LOCATE_SEARCH_TYPE t, EFI_GUID *g, VOID *key, UINTN *n,
                                 EFI_HANDLE **out) {
  assert(t == ByProtocol && !memcmp(g, &FsGuid, sizeof(*g)) && !key);
  *n = Count;
  *out = malloc(sizeof(**out) * (Count ? Count : 1));
  assert(*out);
  Allocations++;
  for (unsigned i = 0; i < Count; i++)
    (*out)[i] = (EFI_HANDLE)(UINTN)(i + 1);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *g, VOID **out) {
  if (!memcmp(g, &LoadedGuid, sizeof(*g))) {
    assert(h == (EFI_HANDLE)(UINTN)99);
    *out = &Loaded;
    Loaded.ImageCodeType = BadLoaded ? EfiBootServicesCode : EfiLoaderCode;
    return EFI_SUCCESS;
  }
  assert((UINTN)h >= 1 && (UINTN)h <= Count);
  if (!memcmp(g, &PathGuid, sizeof(*g))) {
    Path.hd.Header.Length[0] = Malformed ? 3 : sizeof(Path.hd);
    *out = Usb ? (VOID *)&Path : (VOID *)&Path.hd;
    return EFI_SUCCESS;
  }
  assert(!memcmp(g, &FsGuid, sizeof(*g)));
  *out = &Fs;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Pool(EFI_MEMORY_TYPE t, UINTN n, VOID **p) {
  assert(t == EfiLoaderData);
  *p = malloc(n);
  assert(*p);
  Allocations++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Free(VOID *p) {
  assert(p && Allocations);
  Allocations--;
  free(p);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_FILE_PROTOCOL *f) {
  assert(f == &Root || f == &File || f == &Report);
  if (f == &Report) {
    Closes++;
    return FailClose ? EFI_DEVICE_ERROR : EFI_SUCCESS;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open(EFI_FILE_PROTOCOL *root, EFI_FILE_PROTOCOL **f, CHAR16 *name,
                              UINT64 mode, UINT64 attrs) {
  assert(root == &Root && mode == EFI_FILE_MODE_READ && !attrs &&
         !memcmp(name, WindowsPath, sizeof(WindowsPath)));
  if (Present) {
    Present--;
    *f = &File;
    return EFI_SUCCESS;
  }
  *f = NULL;
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI Volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs, EFI_FILE_PROTOCOL **out) {
  assert(fs == &Fs);
  *out = &Root;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Info(EFI_FILE_PROTOCOL *f, EFI_GUID *g, UINTN *n, VOID *out) {
  assert(f == &File && !memcmp(g, &InfoGuid, sizeof(*g)) && *n >= sizeof(EFI_FILE_INFO));
  memset(out, 0, *n);
  *n = sizeof(EFI_FILE_INFO);
  EFI_FILE_INFO *i = out;
  i->FileSize = Empty ? 0 : 4096;
  i->Attribute = Directory ? EFI_FILE_DIRECTORY : 0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Load(BOOLEAN policy, EFI_HANDLE parent, EFI_DEVICE_PATH_PROTOCOL *path,
                              VOID *data, UINTN bytes, EFI_HANDLE *image) {
  assert(!policy && parent == Context.image && !data && !bytes);
  Loads++;
  assert(path->Type == MEDIA_DEVICE_PATH && path->SubType == MEDIA_HARDDRIVE_DP);
  EFI_DEVICE_PATH_PROTOCOL *file = (VOID *)((UINT8 *)path + sizeof(HARDDRIVE_DEVICE_PATH));
  assert(file->Type == MEDIA_DEVICE_PATH && file->SubType == MEDIA_FILEPATH_DP &&
         !memcmp((UINT8 *)file + 4, WindowsPath, sizeof(WindowsPath)));
  EFI_DEVICE_PATH_PROTOCOL *end = (VOID *)((UINT8 *)file + 4 + sizeof(WindowsPath));
  assert(end->Type == END_DEVICE_PATH_TYPE && end->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE);
  *image = MissingImage ? NULL : (EFI_HANDLE)(UINTN)99;
  return LoadStatus;
}
static EFI_STATUS EFIAPI Start(EFI_HANDLE h, UINTN *n, CHAR16 **data) {
  assert(h == Plan.image && !n && !data && Closes == (Context.quiet ? 0 : 1));
  Starts++;
  return StartStatus;
}
static EFI_STATUS EFIAPI Unload(EFI_HANDLE h) {
  assert(h == (EFI_HANDLE)(UINTN)99);
  Unloads++;
  return UnloadStatus;
}
static void Setup(void) {
  memset(&Bs, 0, sizeof(Bs));
  memset(&Table, 0, sizeof(Table));
  memset(&Plan, 0, sizeof(Plan));
  memset(&Context, 0, sizeof(Context));
  Bs.LocateHandleBuffer = Handles;
  Bs.HandleProtocol = Handle;
  Bs.AllocatePool = Pool;
  Bs.FreePool = Free;
  Bs.LoadImage = Load;
  Bs.StartImage = Start;
  Bs.UnloadImage = Unload;
  Table.BootServices = &Bs;
  Context.table = &Table;
  Context.image = (EFI_HANDLE)(UINTN)55;
  Context.log = Log;
  memset(&Fs, 0, sizeof(Fs));
  memset(&Root, 0, sizeof(Root));
  memset(&File, 0, sizeof(File));
  memset(&Report, 0, sizeof(Report));
  Fs.OpenVolume = Volume;
  Root.Open = Open;
  Root.Close = File.Close = Report.Close = Close;
  File.GetInfo = Info;
  memset(&Path, 0, sizeof(Path));
  Path.usb.Header.Type = MESSAGING_DEVICE_PATH;
  Path.usb.Header.SubType = MSG_USB_DP;
  Path.usb.Header.Length[0] = sizeof(Path.usb);
  Path.hd.Header.Type = MEDIA_DEVICE_PATH;
  Path.hd.Header.SubType = MEDIA_HARDDRIVE_DP;
  Path.hd.Header.Length[0] = sizeof(Path.hd);
  Path.end.Type = END_DEVICE_PATH_TYPE;
  Path.end.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  Path.end.Length[0] = 4;
  memset(&Loaded, 0, sizeof(Loaded));
  Loaded.DeviceHandle = (EFI_HANDLE)(UINTN)1;
  Count = Present = 1;
  Loads = Starts = Unloads = Allocations = Closes = 0;
  Usb = Malformed = MissingImage = BadLoaded = FailLog = FailClose = Directory = Empty = FALSE;
  LoadStatus = StartStatus = UnloadStatus = EFI_SUCCESS;
}
int main(void) {
  Setup();
  assert(GwPrepare(&Context, &Plan) == EFI_SUCCESS && Loads == 1 && Plan.image && !Allocations);
  assert(GwCancel(&Context, &Plan) == EFI_SUCCESS && Unloads == 1 && !Plan.image);
  Setup();
  Present = 0;
  assert(GwPrepare(&Context, &Plan) == EFI_NOT_FOUND && !Loads && !Allocations);
  Setup();
  Count = Present = 2;
  assert(GwPrepare(&Context, &Plan) == EFI_UNSUPPORTED && !Loads && !Allocations);
  Setup();
  Usb = TRUE;
  assert(GwPrepare(&Context, &Plan) == EFI_NOT_FOUND && !Loads && !Allocations);
  Setup();
  Malformed = TRUE;
  assert(GwPrepare(&Context, &Plan) == EFI_NOT_FOUND && !Loads && !Allocations);
  Setup();
  Count = 129;
  assert(GwPrepare(&Context, &Plan) == EFI_UNSUPPORTED && !Loads && !Allocations);
  Setup();
  LoadStatus = EFI_SECURITY_VIOLATION;
  assert(GwPrepare(&Context, &Plan) == EFI_SECURITY_VIOLATION && Unloads == 1 && !Plan.image &&
         !Allocations);
  Setup();
  MissingImage = TRUE;
  assert(GwPrepare(&Context, &Plan) != EFI_SUCCESS && !Plan.image && !Allocations);
  Setup();
  BadLoaded = TRUE;
  assert(GwPrepare(&Context, &Plan) == EFI_COMPROMISED_DATA && Unloads == 1 && !Allocations);
  Setup();
  Directory = TRUE;
  assert(GwPrepare(&Context, &Plan) == EFI_COMPROMISED_DATA && !Loads && !Allocations);
  Setup();
  Empty = TRUE;
  assert(GwPrepare(&Context, &Plan) == EFI_COMPROMISED_DATA && !Loads && !Allocations);
  Setup();
  FailLog = TRUE;
  assert(GwPrepare(&Context, &Plan) == EFI_DEVICE_ERROR && !Loads && !Allocations);
  Setup();
  Bs.StartImage = NULL;
  assert(GwPrepare(&Context, &Plan) == EFI_UNSUPPORTED && !Loads);
  Setup();
  Plan.image = (EFI_HANDLE)(UINTN)99;
  UnloadStatus = EFI_DEVICE_ERROR;
  assert(GwCancel(&Context, &Plan) == EFI_DEVICE_ERROR && Plan.image);
  Setup();
  EFI_FILE_PROTOCOL *report = &Report;
  Plan.image = (EFI_HANDLE)(UINTN)99;
  assert(GwStart(&Context, &Plan, &report, FALSE) == EFI_ACCESS_DENIED && !Starts && !Closes &&
         report);
  Setup();
  report = &Report;
  assert(GwStart(&Context, &Plan, &report, TRUE) == EFI_ACCESS_DENIED && !Starts && !Closes);
  Setup();
  Plan.image = (EFI_HANDLE)(UINTN)99;
  report = &Report;
  FailLog = TRUE;
  assert(GwStart(&Context, &Plan, &report, TRUE) == EFI_DEVICE_ERROR && !Starts && !Closes &&
         report);
  Setup();
  Plan.image = (EFI_HANDLE)(UINTN)99;
  report = &Report;
  FailClose = TRUE;
  assert(GwStart(&Context, &Plan, &report, TRUE) == EFI_DEVICE_ERROR && !Starts && Closes == 1 &&
         !report);
  Setup();
  Plan.image = (EFI_HANDLE)(UINTN)99;
  report = &Report;
  StartStatus = EFI_LOAD_ERROR;
  assert(GwStart(&Context, &Plan, &report, TRUE) == EFI_LOAD_ERROR && Starts == 1 && !report);
  Setup();
  Plan.image = (EFI_HANDLE)(UINTN)99;
  report = &Report;
  assert(GwStart(&Context, &Plan, &report, TRUE) == EFI_ABORTED && Starts == 1 && !report);
  Setup();
  Context.quiet = TRUE;
  Plan.image = (EFI_HANDLE)(UINTN)99;
  report = NULL;
  assert(GwStart(&Context, &Plan, &report, TRUE) == EFI_ABORTED && Starts == 1 && !Closes);
  Setup();
  Context.quiet = TRUE;
  Plan.image = (EFI_HANDLE)(UINTN)99;
  report = NULL;
  assert(GwStart(&Context, &Plan, &report, FALSE) == EFI_ACCESS_DENIED && !Starts);
  puts("PASS 22 Windows discovery/load/handoff scenarios; no disk writes or NVRAM services "
       "installed");
  return 0;
}
