#include "cycle.h"
#if defined(GC_FULL_FLR)
#include "full.h"
#define GC_DIAG "v8-virtual-gop"
#define GC_DIAG_WIDE L"v8-virtual-gop"
#elif defined(GC_LEAF_ONLY)
#include "leaf.h"
#define GC_DIAG "v5-leaf"
#define GC_DIAG_WIDE L"v5-leaf"
#elif defined(GC_TREE_ONLY)
#include "tree.h"
#define GC_DIAG "v4-tree"
#define GC_DIAG_WIDE L"v4-tree"
#else
#define GC_DIAG "v3"
#define GC_DIAG_WIDE L"v3"
#endif
#include "../../unlock/format.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>

static EFI_SYSTEM_TABLE *Table;
static EFI_FILE_PROTOCOL *Report;
static BOOLEAN LogFailed;
static CHAR16 *volatile Banner=L"\r\nNVIDIA GOP experiment / diagnostic " GC_DIAG_WIDE L"\r\n";
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,
  LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
  InfoGuid=EFI_FILE_INFO_ID,GlobalGuid=EFI_GLOBAL_VARIABLE;

/* Logging deliberately never calls a console, including error and recovery paths. */
static BOOLEAN Log(const char *format,...) {
  char data[512];va_list args;va_start(args,format);
  size_t size=GuFormatV(data,sizeof(data),format,args);va_end(args);
  if(LogFailed || !Report || size>=sizeof(data)){LogFailed=TRUE;return FALSE;}
  UINTN written=size;EFI_STATUS status=Report->Write(Report,&written,data);
  if(EFI_ERROR(status) || written!=size || EFI_ERROR(Report->Flush(Report)))LogFailed=TRUE;
  return !LogFailed;
}
static EFI_STATUS Say(CHAR16 *text) {
  if(!Table->ConOut || !Table->ConOut->OutputString)return EFI_UNSUPPORTED;
  return Table->ConOut->OutputString(Table->ConOut,text);
}
static VOID ShowFailure(const char *stage,EFI_STATUS status) {
  static const CHAR16 digits[]=L"0123456789abcdef";
  CHAR16 number[]=L"0x0000000000000000\r\n";
  CHAR16 name[64];UINTN i=0;
  while(stage[i] && i<63){name[i]=(UINT8)stage[i];i++;}name[i]=0;
  for(i=0;i<16;i++)number[2+i]=digits[((UINT64)status>>((15-i)*4))&15];
  Say(L"\r\nGOP diagnostic " GC_DIAG_WIDE L": no detach attempted.\r\nStage: ");Say(name);
  Say(L"\r\nStatus: ");Say(number);
  Say(L"Check USB RTX30-GOP-TEST/RUNxx/REPORT.TXT.\r\n"
    L"For usb-log failure, copy the entire package including RTX30-GOP-TEST.\r\n"
    L"Returning to firmware in 20 seconds.\r\n");
  Table->BootServices->Stall(20000000);
}
static BOOLEAN Usb(EFI_HANDLE device) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(EFI_ERROR(Table->BootServices->HandleProtocol(device,&PathGuid,(VOID **)&p)) || !p)return FALSE;
  BOOLEAN usb=FALSE;UINTN bytes=0;
  for(UINTN i=0;i<64 && bytes+4<=4096;i++) {
    UINTN length=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(length<4 || length>4096-bytes)return FALSE;
    if(p->Type==MESSAGING_DEVICE_PATH && p->SubType==MSG_USB_DP && length==sizeof(USB_DEVICE_PATH))usb=TRUE;
    if(p->Type==END_DEVICE_PATH_TYPE)return usb && length==4 && p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE;
    bytes+=length;p=(VOID *)((UINT8 *)p+length);
  }
  return FALSE;
}
static EFI_STATUS CloseFile(EFI_FILE_PROTOCOL *file,EFI_STATUS status) {
  if(file && EFI_ERROR(file->Close(file)))return EFI_DEVICE_ERROR;
  return status;
}
static EFI_STATUS OpenLog(EFI_HANDLE image) {
  EFI_BOOT_SERVICES *bs=Table->BootServices;EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL,*parent=NULL,*run=NULL;
  EFI_STATUS status=bs->HandleProtocol(image,&LoadedGuid,(VOID **)&loaded);
  if(EFI_ERROR(status) || !loaded || !Usb(loaded->DeviceHandle))return EFI_UNSUPPORTED;
  status=bs->HandleProtocol(loaded->DeviceHandle,&FsGuid,(VOID **)&fs);
  if(EFI_ERROR(status) || !fs)return EFI_NOT_FOUND;
  status=fs->OpenVolume(fs,&root);if(EFI_ERROR(status) || !root)return EFI_NOT_FOUND;
  status=root->Open(root,&parent,L"\\RTX30-GOP-TEST",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
  status=CloseFile(root,status);if(EFI_ERROR(status))return CloseFile(parent,status);
  if(!parent)return EFI_NOT_FOUND;
  union {EFI_FILE_INFO info;UINT8 bytes[512];} metadata;UINTN size=sizeof(metadata);
  status=parent->GetInfo(parent,&InfoGuid,&size,&metadata);
  if(EFI_ERROR(status) || size<SIZE_OF_EFI_FILE_INFO || !(metadata.info.Attribute&EFI_FILE_DIRECTORY))return CloseFile(parent,EFI_UNSUPPORTED);
  status=EFI_OUT_OF_RESOURCES;
  for(UINTN slot=0;slot<100;slot++) {
    CHAR16 name[]=L"RUN00";name[3]=(CHAR16)('0'+slot/10);name[4]=(CHAR16)('0'+slot%10);
    EFI_FILE_PROTOCOL *existing=NULL;
    status=parent->Open(parent,&existing,name,EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)) {status=CloseFile(existing,EFI_SUCCESS);if(EFI_ERROR(status))break;status=EFI_OUT_OF_RESOURCES;continue;}
    if(status!=EFI_NOT_FOUND)break;
    status=parent->Open(parent,&run,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);break;
  }
  status=CloseFile(parent,status);if(EFI_ERROR(status))return CloseFile(run,status);
  if(!run)return EFI_OUT_OF_RESOURCES;
  status=run->Open(run,&Report,L"REPORT.TXT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
  return CloseFile(run,status);
}
static EFI_STATUS Target(GC_CONTEXT *c) {
  EFI_HANDLE *handles=NULL;UINTN count=0,matches=0;EFI_BOOT_SERVICES *bs=Table->BootServices;
  EFI_STATUS status=bs->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&handles);
  if(EFI_ERROR(status))return status;
  if(!handles || !count || count>1024)status=EFI_UNSUPPORTED;
  for(UINTN i=0;!EFI_ERROR(status) && i<count;i++) {
    EFI_PCI_IO_PROTOCOL *pci=NULL;UINT32 cfg[64];
    status=bs->HandleProtocol(handles[i],&PciGuid,(VOID **)&pci);
    if(EFI_ERROR(status) || !pci || !pci->Pci.Read || !pci->GetLocation){status=EFI_UNSUPPORTED;break;}
    status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,64,cfg);if(EFI_ERROR(status))break;
    if((cfg[0]&0xffff)!=0x10de || (cfg[2]>>24)!=3)continue;
    matches++;
    c->target=handles[i];c->pci=pci;
    c->identity=cfg[0];c->subsystem=cfg[11];c->class_revision=cfg[2];
    status=pci->GetLocation(pci,&c->location[0],&c->location[1],&c->location[2],&c->location[3]);
    if(!EFI_ERROR(status) && !Log("target=%#llx BDF=%zu:%zu:%zu.%zu\n",(unsigned long long)(UINTN)c->target,
      (size_t)c->location[0],(size_t)c->location[1],(size_t)c->location[2],(size_t)c->location[3]))status=EFI_DEVICE_ERROR;
    for(UINTN j=0;!EFI_ERROR(status) && j<64;j++)if(!Log("config_before[%02x]=%08x\n",(unsigned)(j*4),cfg[j]))status=EFI_DEVICE_ERROR;
  }
  if(handles && EFI_ERROR(bs->FreePool(handles)))status=EFI_DEVICE_ERROR;
  return !EFI_ERROR(status) && matches!=1?EFI_UNSUPPORTED:status;
}
#ifndef GC_TREE_ONLY
static EFI_STATUS Approve(VOID) {
  if(!Table->ConIn || !Table->ConIn->ReadKeyStroke)return EFI_UNSUPPORTED;
  EFI_STATUS status=Table->ConIn->Reset(Table->ConIn,FALSE);if(EFI_ERROR(status))return status;
  status=Say(
#ifdef GC_FULL_FLR
    L"FULL GPU GOP release and ONE PCIe FLR, then display recovery.\r\n"
    L"EDID consumers are stopped per device; shared input is checked after every step.\r\n"
    L"No compute unlock is linked. FLR will reset the GPU and may blank the screen.\r\n"
#elif defined(GC_LEAF_ONLY)
    L"Detach/restore ONE text-output member. GPU GOP and EDID drivers stay bound.\r\n"
    L"The selected output driver will receive Stop, then Supported/Start for recovery.\r\n"
#else
    L"One NVIDIA display device. GOP driver Stop/Start may change GPU state.\r\n"
#endif
#ifndef GC_FULL_FLR
    L"No unlock, FLR or direct register writes are linked.\r\n"
#endif
    L"Screen may go black. If it does not return within 60 seconds, power off physically.\r\n"
    L"After recovery, visually check the screen and power off; do not run Unlock in this boot.\r\n"
    L"Press U once for one detach/reconnect; Esc or 30 seconds cancels.\r\n");
  if(EFI_ERROR(status))return status;
  for(UINTN i=0;i<300;i++) {
    EFI_INPUT_KEY key;status=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(!EFI_ERROR(status)) {
      if(key.UnicodeChar=='U' || key.UnicodeChar=='u')return EFI_SUCCESS;
      if(key.ScanCode==0x17)return EFI_ABORTED;
    } else if(status!=EFI_NOT_READY)return status;
    Table->BootServices->Stall(100000);
  }
  return EFI_TIMEOUT;
}
static VOID Halt(VOID) {
  /* No stale console access or automatic boot continuation after uncertain recovery. */
  for(;;)Table->BootServices->Stall(1000000);
}
#endif
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *table) {
  if(!table || !table->BootServices || !table->RuntimeServices)return EFI_INVALID_PARAMETER;
  Table=table;Report=NULL;LogFailed=FALSE;
  GC_CONTEXT context={0};context.table=table;context.image=image;context.log=Log;
#ifndef GC_TREE_ONLY
#ifdef GC_FULL_FLR
  GF_PLAN *approved=NULL;
#elif defined(GC_LEAF_ONLY)
  GL_PLAN approved;
#else
  GC_SNAPSHOT approved;
#endif
  GC_RESULT result={0};
#endif
  const char *stage="usb-log";
  EFI_STATUS status=OpenLog(image);
  if(EFI_ERROR(status)){if(Report)Report->Close(Report);Report=NULL;ShowFailure(stage,status);return status;}
  /* Persist entry before the first console call, including a failing console. */
  if(!Log("ENTRY: GOP diagnostic " GC_DIAG "; mode="
#ifdef GC_TREE_ONLY
    "read-only ownership tree; no driver Start/Stop, exclusive access, unlock or FLR linked\n"
#else
#ifdef GC_FULL_FLR
    "full GPU GOP release, one FLR, PCI/GOP restore; no compute unlock\n"
#elif defined(GC_LEAF_ONLY)
    "single text member Stop/Supported/Start; no GPU GOP release, exclusive access, unlock or FLR\n"
#else
    "detach/reconnect; no unlock/FLR/direct GPU writes linked; driver Stop/Start may write hardware\n"
#endif
#endif
    )) {
    status=EFI_DEVICE_ERROR;goto finish;
  }
  stage="console";status=Say(Banner);
  if(!Log("STAGE console status=%#llx\n",(unsigned long long)status)){status=EFI_DEVICE_ERROR;goto finish;}
  if(EFI_ERROR(status))goto finish;
  UINT8 secure=0xff;UINTN size=1;
  stage="secure-boot";
  status=table->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,NULL,&size,&secure);
  Log("STAGE secure-boot query=%#llx bytes=%zu enabled=%u\n",(unsigned long long)status,(size_t)size,secure);
  if(EFI_ERROR(status) || size!=1 || secure){status=EFI_SECURITY_VIOLATION;goto finish;}
  stage="watchdog";
  status=table->BootServices->SetWatchdogTimer(0,0,0,NULL);if(EFI_ERROR(status))goto finish;
  stage="target";
  status=Target(&context);if(EFI_ERROR(status))goto finish;
#ifdef GC_TREE_ONLY
  stage="ownership-tree";
  Say(L"Read-only ownership tree capture. No U key or driver detach is used.\r\n");
  status=GcReadTree(&context);
  if(!Log("TREE_RESULT status=%#llx touched=0\n",(unsigned long long)status))status=EFI_DEVICE_ERROR;
  status=CloseFile(Report,status);Report=NULL;
  if(EFI_ERROR(status)){ShowFailure(stage,status);return status;}
  Say(L"Read-only capture completed. Saved RTX30-GOP-TEST/RUNxx/REPORT.TXT.\r\n"
    L"No drivers were detached. Returning to firmware in 20 seconds.\r\n");
  Table->BootServices->Stall(20000000);return EFI_SUCCESS;
#else
#ifdef GC_FULL_FLR
  stage="full-plan-allocation";status=table->BootServices->AllocatePool(EfiLoaderData,sizeof(*approved),(VOID **)&approved);
  if(EFI_ERROR(status) || !approved){status=EFI_OUT_OF_RESOURCES;goto finish;}
  stage="full-prepare";status=GfPrepare(&context,approved);if(EFI_ERROR(status))goto finish;
  stage="manual-approval";status=Approve();if(EFI_ERROR(status))goto finish;
  if(!Log("MANUAL_U confirmed for full GOP release and one FLR\n")){status=EFI_DEVICE_ERROR;goto finish;}
  stage="full-stop-flr-restore";status=GfRun(&context,approved,&result);
  Log("FULL_FINAL status=%#llx touched=%u restored=%u\n",(unsigned long long)status,result.touched,result.restored);
#elif defined(GC_LEAF_ONLY)
  stage="leaf-prepare";status=GcLeafPrepare(&context,&approved);if(EFI_ERROR(status))goto finish;
  stage="manual-approval";status=Approve();if(EFI_ERROR(status))goto finish;
  if(!Log("MANUAL_U confirmed for one text member\n")){status=EFI_DEVICE_ERROR;goto finish;}
  stage="leaf-stop-restore";status=GcLeafRun(&context,&approved,&result);
  Log("LEAF_FINAL status=%#llx touched=%u stop=%#llx isolated=%#llx restore=%#llx verify=%#llx restored=%u\n",
    (unsigned long long)status,result.touched,(unsigned long long)result.detach,(unsigned long long)result.isolated,
    (unsigned long long)result.connect,(unsigned long long)result.verify,result.restored);
#else
  stage="gop-ownership";
  status=GcCapture(&context,&approved);if(EFI_ERROR(status))goto finish;
  if(!Log("capture summary owner=%#llx children=%zu drivers=%zu physical_gops=%zu text_outputs=%zu\n",
    (unsigned long long)(UINTN)approved.owner,(size_t)approved.child_count,(size_t)approved.driver_count,
    (size_t)approved.physical_gops,(size_t)approved.text_outputs)){status=EFI_DEVICE_ERROR;goto finish;}
  if(!GcReady(&approved)){status=EFI_UNSUPPORTED;goto finish;}
  if(!Log("READY owner=%#llx children=%zu drivers=%zu\n",(unsigned long long)(UINTN)approved.owner,
    (size_t)approved.child_count,(size_t)approved.driver_count)){status=EFI_DEVICE_ERROR;goto finish;}
  stage="manual-approval";status=Approve();if(EFI_ERROR(status))goto finish;
  if(!Log("MANUAL_U confirmed\n")){status=EFI_DEVICE_ERROR;goto finish;}
  stage="pre-detach-recheck";status=GcCycle(&context,&approved,&result);
  Log("RESULT detach=%#llx isolated=%#llx claim=%#llx close=%#llx connect=%#llx verify=%#llx restored=%u\n",
    (unsigned long long)result.detach,(unsigned long long)result.isolated,(unsigned long long)result.claim,
    (unsigned long long)result.close,(unsigned long long)result.connect,(unsigned long long)result.verify,result.restored);
#endif
  if(result.touched && (!result.restored || LogFailed))goto stop;
  if(result.restored) {
    EFI_STATUS output=Say(
#ifdef GC_FULL_FLR
      L"GPU GOP restored after FLR. Saving PCI report; wait for REPORT SAVED.\r\n"
#elif defined(GC_LEAF_ONLY)
      L"Text-output member restored; GPU GOP, input and ownership guards matched.\r\n"
      L"Check LEAF_RESULT for experiment status and verify this screen visually.\r\n"
#else
      L"GOP/text protocols restored. Verify this screen visually.\r\n"
#endif
#ifdef GC_FULL_FLR
      L"No compute unlock was attempted. Do not reset or remove the USB yet.\r\n");
#else
      L"No unlock or FLR was attempted. Power off before the next experiment.\r\n");
#endif
    if(!Log("console_output_status=%#llx\n",(unsigned long long)output) || EFI_ERROR(output))goto stop;
    UINT32 cfg[64];EFI_STATUS read=context.pci->Pci.Read(context.pci,EfiPciIoWidthUint32,0,64,cfg);
    if(!Log("config_after_status=%#llx\n",(unsigned long long)read) || EFI_ERROR(read))goto stop;
    for(UINTN i=0;i<64;i++)if(!Log("config_after[%02x]=%08x\n",(unsigned)(i*4),cfg[i]))goto stop;
    if(!Log("END: protocol checks complete; visual confirmation pending; power off before any unlock\n"))goto stop;
    EFI_STATUS closed=Report->Close(Report);Report=NULL;
    if(EFI_ERROR(closed))Halt();
    Say(L"REPORT SAVED. Power off now before any further experiment.\r\n");
    /* This experiment never automatically continues to Windows after touching GOP. */
    Halt();
  }
#endif
finish:
  Log("CANCELLED_OR_REFUSED stage=%s status=%#llx touched=0\n",stage,(unsigned long long)status);
  if(LogFailed)status=EFI_DEVICE_ERROR;
  status=CloseFile(Report,status);Report=NULL;
#ifdef GC_FULL_FLR
  if(approved && EFI_ERROR(table->BootServices->FreePool(approved)))status=EFI_DEVICE_ERROR;
#endif
  ShowFailure(stage,status);
  return status;
#ifndef GC_TREE_ONLY
stop:
  Log("STOP: recovery or logging uncertain. Power off physically; do not continue boot.\n");
  if(Report){Report->Close(Report);Report=NULL;}
  Halt();return EFI_DEVICE_ERROR;
#endif
}
