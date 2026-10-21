/* Include the standalone app to exercise its actual logger and approval gate. */
#include "app.c"
static EFI_GUID PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned Writes, Flushes, Outputs, Polls;
static BOOLEAN Short, FailWrite, FailFlush;
static unsigned ReturnDelays, FirstOutputWrites;
static EFI_STATUS OutputStatus;
static char SavedLog[8192];
static size_t SavedBytes;
static EFI_STATUS InputStatus;
static CHAR16 Key;
static EFI_STATUS EFIAPI Write(EFI_FILE_PROTOCOL *file, UINTN *bytes, VOID *buffer) {
  (void)file;
  assert(buffer && *bytes);
  Writes++;
  if (Short) (*bytes)--;
  if (SavedBytes + *bytes < sizeof(SavedLog)) {
    memcpy(SavedLog + SavedBytes, buffer, *bytes);
    SavedBytes += *bytes;
    SavedLog[SavedBytes] = 0;
  }
  return FailWrite ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush(EFI_FILE_PROTOCOL *file) {
  (void)file;
  Flushes++;
  return FailFlush ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *text) {
  (void)console;
  (void)text;
  if (!Outputs) FirstOutputWrites = Writes;
  Outputs++;
  return OutputStatus;
}
static EFI_STATUS EFIAPI ResetInput(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input, BOOLEAN extended) {
  (void)input;
  (void)extended;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReadKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input, EFI_INPUT_KEY *key) {
  (void)input;
  Polls++;
  key->UnicodeChar = Key;
  key->ScanCode = Key == 0x1b ? 0x17 : 0;
  return InputStatus;
}
static EFI_STATUS EFIAPI Stall(UINTN us) {
  assert(us == 100000 || us == 20000000);
  if (us == 20000000) ReturnDelays++;
  return EFI_SUCCESS;
}
static EFI_FILE_PROTOCOL File;
static EFI_SYSTEM_TABLE System;
static EFI_BOOT_SERVICES Services;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Console;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static EFI_PCI_IO_PROTOCOL TestPci;
static UINT32 DeviceIdentity, DeviceSubsystem;
static UINTN DeviceCount;
static EFI_STATUS EFIAPI TestRead(EFI_PCI_IO_PROTOCOL *pci, EFI_PCI_IO_PROTOCOL_WIDTH width,
                                  UINT32 offset, UINTN count, VOID *data) {
  (void)pci;
  assert(width == EfiPciIoWidthUint32);
  if (count == 1) {
    assert(offset == 0 || offset == 8 || offset == 0x2c);
    *(UINT32 *)data = offset == 0 ? DeviceIdentity : offset == 8 ? 0x030000a1U : DeviceSubsystem;
  } else {
    assert(offset == 0 && count == 64);
    memset(data, 0, 256);
    ((UINT32 *)data)[0] = DeviceIdentity;
    ((UINT32 *)data)[2] = 0x030000a1U;
    ((UINT32 *)data)[11] = DeviceSubsystem;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestLocation(EFI_PCI_IO_PROTOCOL *pci, UINTN *s, UINTN *b, UINTN *d,
                                      UINTN *f) {
  (void)pci;
  *s = 0;
  *b = 7;
  *d = 0;
  *f = 0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestHandle(EFI_HANDLE h, EFI_GUID *guid, VOID **out) {
  assert((UINTN)h >= 1 && (UINTN)h <= DeviceCount && !memcmp(guid, &PciGuid, sizeof(*guid)));
  *out = &TestPci;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestHandles(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *guid, VOID *key,
                                     UINTN *count, EFI_HANDLE **out) {
#if defined(GC_LEAF_ONLY) || defined(GC_FULL_FLR)
  EFI_GUID gop = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  if (!memcmp(guid, &gop, sizeof(gop))) {
    *count = 0;
    *out = NULL;
    return EFI_NOT_FOUND;
  }
#endif
  (void)key;
  assert(type == ByProtocol && !memcmp(guid, &PciGuid, sizeof(*guid)));
  *count = DeviceCount;
  *out = calloc(DeviceCount, sizeof(**out));
  assert(*out);
  for (UINTN i = 0; i < DeviceCount; i++)
    (*out)[i] = (EFI_HANDLE)(i + 1);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestFree(VOID *data) {
  free(data);
  return EFI_SUCCESS;
}
#ifdef GC_FULL_FLR
static EFI_STATUS EFIAPI TestPool(EFI_MEMORY_TYPE type, UINTN bytes, VOID **out) {
  assert(type == EfiLoaderData);
  *out = malloc(bytes);
  return *out ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}
#endif
#ifdef GC_TREE_ONLY
static EFI_STATUS EFIAPI TreeLocate(EFI_GUID *guid, EFI_DEVICE_PATH_PROTOCOL **path,
                                    EFI_HANDLE *handle) {
  (void)path;
  assert(!memcmp(guid, &PciGuid, sizeof(*guid)));
  *handle = (EFI_HANDLE)(UINTN)1;
  return EFI_SUCCESS;
}
#endif
static VOID Setup(VOID) {
  Quiet = FALSE;
  Writes = Flushes = Outputs = Polls = 0;
  Short = FailWrite = FailFlush = LogFailed = FALSE;
  ReturnDelays = FirstOutputWrites = 0;
  OutputStatus = EFI_SUCCESS;
  SavedBytes = 0;
  SavedLog[0] = 0;
  memset(&File, 0, sizeof(File));
  File.Write = Write;
  File.Flush = Flush;
  Report = &File;
  memset(&System, 0, sizeof(System));
  memset(&Services, 0, sizeof(Services));
  memset(&Console, 0, sizeof(Console));
  Console.OutputString = Output;
  memset(&Input, 0, sizeof(Input));
  Input.Reset = ResetInput;
  Input.ReadKeyStroke = ReadKey;
  System.ConOut = &Console;
  System.ConIn = &Input;
  System.BootServices = &Services;
  Services.Stall = Stall;
  Table = &System;
  Key = 'u';
  InputStatus = EFI_SUCCESS;
  memset(&TestPci, 0, sizeof(TestPci));
  TestPci.Pci.Read = TestRead;
  TestPci.GetLocation = TestLocation;
  Services.HandleProtocol = TestHandle;
  Services.LocateHandleBuffer = TestHandles;
  Services.FreePool = TestFree;
#ifdef GC_FULL_FLR
  Services.AllocatePool = TestPool;
#endif
#ifdef GC_TREE_ONLY
  Services.LocateDevicePath = TreeLocate;
#endif
  DeviceCount = 1;
  DeviceIdentity = 0x278610deU;
  DeviceSubsystem = 0x12345678U;
}
static EFI_FILE_PROTOCOL RootFile, ParentFile, RunFile;
static EFI_LOADED_IMAGE_PROTOCOL LoadedImage;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_RUNTIME_SERVICES Runtime;
static struct {
  USB_DEVICE_PATH usb;
  EFI_DEVICE_PATH_PROTOCOL end;
} UsbPath;
static BOOLEAN MissingMarker, DebugMarker, DebugOpenError;
static EFI_FILE_PROTOCOL DebugFile;
static UINT8 SecureEnabled;
static unsigned ReportCloses;
static UINTN OccupiedRuns;
static BOOLEAN GopExists;
static EFI_STATUS EFIAPI FileClose(EFI_FILE_PROTOCOL *file) {
  if (file == &File) ReportCloses++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs, EFI_FILE_PROTOCOL **root) {
  assert(fs == &Fs);
  *root = &RootFile;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI RootOpen(EFI_FILE_PROTOCOL *root, EFI_FILE_PROTOCOL **file, CHAR16 *name,
                                  UINT64 mode, UINT64 attr) {
  static const CHAR16 expected[] = L"\\RTX-UNLOCK";
  assert(!memcmp(name, expected, sizeof(expected)) && root == &RootFile &&
         mode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE) && !attr);
  *file = MissingMarker ? NULL : &ParentFile;
  return MissingMarker ? EFI_NOT_FOUND : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ParentOpen(EFI_FILE_PROTOCOL *parent, EFI_FILE_PROTOCOL **file,
                                    CHAR16 *name, UINT64 mode, UINT64 attr) {
  assert(parent == &ParentFile);
#ifdef GC_PRODUCTION
  if (name[0] == 'D') {
    static const CHAR16 expected[] = L"DEBUG.ON";
    assert(!memcmp(name, expected, sizeof(expected)) && mode == EFI_FILE_MODE_READ && !attr);
    *file = DebugMarker ? &DebugFile : NULL;
    if (DebugOpenError) {
      *file = NULL;
      return EFI_DEVICE_ERROR;
    }
    return DebugMarker ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
#else
  (void)name;
#endif
  assert(name[0] == 'R' && name[1] == 'U' && name[2] == 'N' && !name[5]);
  UINTN slot = (name[3] - '0') * 10 + name[4] - '0';
  if (mode == EFI_FILE_MODE_READ) {
    *file = slot < OccupiedRuns ? &RunFile : NULL;
    return *file ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  assert(attr == EFI_FILE_DIRECTORY);
  *file = &RunFile;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI RunOpen(EFI_FILE_PROTOCOL *run, EFI_FILE_PROTOCOL **file, CHAR16 *name,
                                 UINT64 mode, UINT64 attr) {
  static const CHAR16 expected[] = L"GOP.TXT";
  assert(!memcmp(name, expected, sizeof(expected)) && run == &RunFile && !attr);
  if (mode == EFI_FILE_MODE_READ) {
    *file = GopExists ? &File : NULL;
    return *file ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  *file = &File;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Metadata(EFI_FILE_PROTOCOL *parent, EFI_GUID *guid, UINTN *size,
                                  VOID *buffer) {
  assert(parent == &ParentFile && !memcmp(guid, &InfoGuid, sizeof(*guid)) &&
         *size >= sizeof(EFI_FILE_INFO));
  memset(buffer, 0, *size);
  ((EFI_FILE_INFO *)buffer)->Attribute = EFI_FILE_DIRECTORY;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI StartupHandle(EFI_HANDLE handle, EFI_GUID *guid, VOID **out) {
  (void)handle;
  if (!memcmp(guid, &LoadedGuid, sizeof(*guid)))
    *out = &LoadedImage;
  else if (!memcmp(guid, &FsGuid, sizeof(*guid)))
    *out = &Fs;
  else if (!memcmp(guid, &PathGuid, sizeof(*guid)))
    *out = &UsbPath;
  else if (!memcmp(guid, &PciGuid, sizeof(*guid)))
    *out = &TestPci;
  else {
    *out = NULL;
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Variable(CHAR16 *name, EFI_GUID *guid, UINT32 *attributes, UINTN *size,
                                  VOID *data) {
  (void)name;
  (void)guid;
  assert(!attributes && *size == 1);
  *(UINT8 *)data = SecureEnabled;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Watchdog(UINTN seconds, UINT64 code, UINTN size, CHAR16 *data) {
  assert(!seconds && !code && !size && !data);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DeniedOwners(EFI_HANDLE handle, EFI_GUID ***protocols, UINTN *count) {
  (void)handle;
  *protocols = NULL;
  *count = 0;
  return EFI_ACCESS_DENIED;
}
#ifdef GC_TREE_ONLY
static EFI_STATUS EFIAPI TreeProtocols(EFI_HANDLE handle, EFI_GUID ***protocols, UINTN *count) {
  (void)handle;
  *count = 1;
  *protocols = malloc(sizeof(**protocols));
  assert(*protocols);
  (*protocols)[0] = &PciGuid;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TreeOpens(EFI_HANDLE h, EFI_GUID *g,
                                   EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **entries, UINTN *count) {
  (void)h;
  (void)g;
  *entries = NULL;
  *count = 0;
  return EFI_SUCCESS;
}
#endif
static VOID Startup(VOID) {
  Setup();
  MissingMarker = DebugOpenError = FALSE;
  DebugMarker = TRUE;
  DebugFile.Close = FileClose;
  ReportCloses = 0;
  SecureEnabled = 1;
  OccupiedRuns = 0;
  GopExists = FALSE;
  memset(&RootFile, 0, sizeof(RootFile));
  memset(&ParentFile, 0, sizeof(ParentFile));
  memset(&RunFile, 0, sizeof(RunFile));
  RootFile.Open = RootOpen;
  ParentFile.Open = ParentOpen;
  RunFile.Open = RunOpen;
  ParentFile.GetInfo = Metadata;
  RootFile.Close = ParentFile.Close = RunFile.Close = File.Close = FileClose;
  memset(&LoadedImage, 0, sizeof(LoadedImage));
  LoadedImage.DeviceHandle = (EFI_HANDLE)(UINTN)99;
  memset(&Fs, 0, sizeof(Fs));
  Fs.OpenVolume = Volume;
  memset(&UsbPath, 0, sizeof(UsbPath));
  UsbPath.usb.Header.Type = MESSAGING_DEVICE_PATH;
  UsbPath.usb.Header.SubType = MSG_USB_DP;
  UsbPath.usb.Header.Length[0] = sizeof(USB_DEVICE_PATH);
  UsbPath.end.Type = END_DEVICE_PATH_TYPE;
  UsbPath.end.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  UsbPath.end.Length[0] = 4;
  Services.HandleProtocol = StartupHandle;
  memset(&Runtime, 0, sizeof(Runtime));
  Runtime.GetVariable = Variable;
  System.RuntimeServices = &Runtime;
  Services.SetWatchdogTimer = Watchdog;
  Services.ProtocolsPerHandle = DeniedOwners;
}
int main(void) {
  Setup();
  assert(Log("USB only %u\n", 42) && Writes == 1 && Flushes == 1 && !Outputs);
  Setup();
  System.ConOut = NULL;
  assert(Log("detached\n") && !Outputs);
  Setup();
  Short = TRUE;
  assert(!Log("short\n") && LogFailed && !Outputs);
  assert(!Log("sticky\n") && Writes == 1);
  Setup();
  FailWrite = TRUE;
  assert(!Log("write error\n") && LogFailed && !Outputs);
  Setup();
  FailFlush = TRUE;
  assert(!Log("flush error\n") && LogFailed && !Outputs);
  Setup();
  Report = NULL;
  assert(!Log("missing\n") && LogFailed && !Writes && !Outputs);
  Setup();
  char big[600];
  memset(big, 'x', 599);
  big[599] = 0;
  assert(!Log("%s", big) && !Writes && !Outputs);
#ifndef GC_TREE_ONLY
#ifdef GC_PRODUCTION
  Setup();
  Quiet = TRUE;
  System.ConIn = NULL;
  assert(Approve() == EFI_SUCCESS && !Polls);
  Setup();
  Quiet = TRUE;
  OutputStatus = EFI_DEVICE_ERROR;
  assert(Approve() == EFI_DEVICE_ERROR && !Polls);
  Setup();
  assert(Approve() == EFI_SUCCESS && Polls == 1);
  Setup();
  Key = 0x1b;
  assert(Approve() == EFI_ABORTED && Polls == 1);
  Setup();
  InputStatus = EFI_NOT_READY;
  assert(Approve() == EFI_TIMEOUT && Polls == 300);
  Setup();
  InputStatus = EFI_DEVICE_ERROR;
  assert(Approve() == EFI_DEVICE_ERROR && Polls == 1);
  Setup();
  System.ConIn = NULL;
  assert(Approve() == EFI_UNSUPPORTED && !Polls);
  Startup();
  Report = NULL;
  DebugMarker = FALSE;
  assert(OpenLog((EFI_HANDLE)(UINTN)77) == EFI_SUCCESS && Quiet && !Report && !Writes);
  assert(Log("quiet\n") && !Writes && !Flushes);
  Startup();
  Report = NULL;
  DebugOpenError = TRUE;
  assert(OpenLog((EFI_HANDLE)(UINTN)77) == EFI_DEVICE_ERROR && !Quiet && !Report && !Writes);
  Startup();
  assert(OpenLog((EFI_HANDLE)(UINTN)77) == EFI_SUCCESS && !Quiet && Report);
  assert(Log("debug\n") && Writes == 1 && Flushes == 1);
#else
  Setup();
  assert(Approve() == EFI_SUCCESS && Polls == 1);
  Setup();
  Key = 0x1b;
  assert(Approve() == EFI_ABORTED && Polls == 1);
  Setup();
  InputStatus = EFI_NOT_READY;
  assert(Approve() == EFI_TIMEOUT && Polls == 300);
  Setup();
  InputStatus = EFI_DEVICE_ERROR;
  assert(Approve() == EFI_DEVICE_ERROR && Polls == 1);
  Setup();
  System.ConIn = NULL;
  assert(Approve() == EFI_UNSUPPORTED && !Polls);
#endif
#endif
  Startup();
  Report = NULL;
  OccupiedRuns = 3;
  assert(OpenLog((EFI_HANDLE)(UINTN)77) == EFI_SUCCESS && RunSlot == 3 && Report == &File);
  assert(CloseFile(Report, EFI_SUCCESS) == EFI_SUCCESS);
  Report = NULL;
  Startup();
  Report = NULL;
  OccupiedRuns = 100;
  assert(OpenLog((EFI_HANDLE)(UINTN)77) == EFI_VOLUME_FULL && !Report && !Writes);
  Startup();
  Report = NULL;
  GopExists = TRUE;
  assert(OpenLog((EFI_HANDLE)(UINTN)77) == EFI_ALREADY_STARTED && !Report && !Writes);
  const UINT32 identities[] = {0x250310deU, 0x278610deU, 0x220410deU};
  for (UINTN i = 0; i < sizeof(identities) / sizeof(identities[0]); i++) {
    Setup();
    DeviceIdentity = identities[i];
    GC_CONTEXT context = {0};
    assert(Target(&context) == EFI_SUCCESS && context.identity == DeviceIdentity &&
           context.subsystem == DeviceSubsystem && context.location[1] == 7);
  }
  Setup();
  DeviceCount = 2;
  GC_CONTEXT context = {0};
  assert(Target(&context) == EFI_UNSUPPORTED);
  Setup();
  DeviceIdentity = 0x12341002U;
  assert(Target(&context) == EFI_UNSUPPORTED);
#ifdef GC_PRODUCTION
  puts(
      "PASS production auto gate, quiet/debug marker, logging and target selection; no GPU access");
  return 0;
#else
  Startup();
  MissingMarker = TRUE;
  assert(efi_main((EFI_HANDLE)(UINTN)77, &System) == EFI_NOT_FOUND && !Writes &&
         ReturnDelays == 1 && Outputs && !Report);
  Startup();
  OutputStatus = EFI_DEVICE_ERROR;
  assert(efi_main((EFI_HANDLE)(UINTN)77, &System) == EFI_DEVICE_ERROR && FirstOutputWrites > 0 &&
         ReturnDelays == 1 && ReportCloses == 1);
  assert(strstr(SavedLog, "ENTRY: GOP diagnostic " GC_DIAG) && strstr(SavedLog, "stage=console"));
  Startup();
  assert(efi_main((EFI_HANDLE)(UINTN)77, &System) == EFI_SECURITY_VIOLATION && ReturnDelays == 1 &&
         ReportCloses == 1);
  assert(strstr(SavedLog, "stage=secure-boot") && strstr(SavedLog, "touched=0"));
  Startup();
  SecureEnabled = 0;
#if defined(GC_LEAF_ONLY) || defined(GC_FULL_FLR)
  assert(efi_main((EFI_HANDLE)(UINTN)77, &System) == EFI_NOT_FOUND && ReturnDelays == 1 &&
         ReportCloses == 1 && !Polls);
#else
  assert(efi_main((EFI_HANDLE)(UINTN)77, &System) == EFI_ACCESS_DENIED && ReturnDelays == 1 &&
         ReportCloses == 1);
#endif
#ifdef GC_TREE_ONLY
  assert(strstr(SavedLog, "TREE_RESULT status=0x800000000000000f touched=0"));
  Startup();
  SecureEnabled = 0;
  Services.ProtocolsPerHandle = TreeProtocols;
  Services.OpenProtocolInformation = TreeOpens;
  assert(efi_main((EFI_HANDLE)(UINTN)77, &System) == EFI_SUCCESS && !Polls && ReturnDelays == 1 &&
         ReportCloses == 1);
  assert(strstr(SavedLog, "TREE_END status=0 complete=1") &&
         strstr(SavedLog, "TREE_RESULT status=0 touched=0"));
  puts("PASS 17 read-only app logger/target/startup scenarios; no approval or mutation services");
#elif defined(GC_FULL_FLR)
  assert(strstr(SavedLog, "stage=full-prepare") && strstr(SavedLog, "touched=0"));
  puts("PASS 21 full-FLR app logger/approval/target/startup scenarios; no GPU access");
#elif defined(GC_LEAF_ONLY)
  assert(strstr(SavedLog, "stage=leaf-prepare") && strstr(SavedLog, "touched=0"));
  puts("PASS 21 leaf app logger/approval/target/startup scenarios; no GPU access");
#else
  assert(strstr(SavedLog, "stage=gop-ownership status=0x800000000000000f touched=0"));
  puts("PASS 21 actual app logger/approval/target/startup scenarios; no GPU access");
  return 0;
#endif
  return 0;
#endif
}
