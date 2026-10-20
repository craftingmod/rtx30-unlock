#include "app.h"
#include "app_io.h"
#include "compat/compat.h"
#include "checks.h"
#include "sha256.h"
#include "ownership.h"
#include "../gpu_family.h"
#include "vendor/unlock_610.h"
#include "vendor/gfw_eval.h"
#include <Protocol/GraphicsOutput.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/GlobalVariable.h>
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
  PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
#ifndef GU_GOP_EXPERIMENT
static EFI_GUID GlobalGuid=EFI_GLOBAL_VARIABLE;
#endif
static EFI_SYSTEM_TABLE *Table;
static UINT32 InitialConfig[64];
static const char *Stage;
static GU_RELEASE_OWNER PendingOwner;
static EFI_STATUS Step(EFI_STATUS Status) {
  GuLog("CHECK stage=%s status=%#llx\n",Stage,(unsigned long long)Status);
  return !GuLogReady() && !EFI_ERROR(Status)?EFI_DEVICE_ERROR:Status;
}
static VOID ConfigLog(const UINT32 *Config) {
  for(UINTN i=0;i<64;i+=4)GuLog("config[%02x]=%08x %08x %08x %08x\n",
    (unsigned)(i*4),Config[i],Config[i+1],Config[i+2],Config[i+3]);
}
static VOID GopInfoLog(UINTN Index,const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info) {
  GuLog("GOP[%u] format=%u size=%ux%u stride=%u\n",(unsigned)Index,
    (unsigned)Info->PixelFormat,Info->HorizontalResolution,Info->VerticalResolution,Info->PixelsPerScanLine);
}
static BOOLEAN ConsoleGopCheck(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,UINTN Index,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Output) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode=Gop->Mode;
  if(mode->Info && mode->SizeOfInfo>=sizeof(*mode->Info)) {
    *Output=*mode->Info;GopInfoLog(Index,Output);return Output->PixelFormat==PixelBltOnly;
  }
  /* RUN02's console aggregator leaves SizeOfInfo zero. Never read an unsized
     Info pointer: QueryMode supplies its own sized, callee-allocated buffer.
     The caller restricts this fallback to a pathless, zero-framebuffer GOP. */
  if(mode->SizeOfInfo || !Gop->QueryMode)return FALSE;
  UINT32 number=mode->Mode;UINTN bytes=0;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;
  EFI_STATUS status=Gop->QueryMode(Gop,number,&bytes,&info);
  GuLog("GOP[%u] console QueryMode: status=%#llx mode=%u info_bytes=%zu\n",
    (unsigned)Index,(unsigned long long)status,number,(size_t)bytes);
  BOOLEAN valid=!EFI_ERROR(status) && info && bytes>=sizeof(*info);
  if(valid) {
    *Output=*info;GopInfoLog(Index,Output);
    valid=(info->PixelFormat==PixelBltOnly || info->PixelFormat==PixelRedGreenBlueReserved8BitPerColor ||
      info->PixelFormat==PixelBlueGreenRedReserved8BitPerColor) && Gop->Mode==mode && mode->Mode==number &&
      !mode->FrameBufferBase && !mode->FrameBufferSize;
  }
  if(info) {
    status=Gu.bs->FreePool(info);
    if(EFI_ERROR(status)){GuLog("GOP[%u] query FreePool failed: %#llx\n",(unsigned)Index,(unsigned long long)status);valid=FALSE;}
  }
  return valid;
}
static BOOLEAN DisplayCheck(VOID) {
  EFI_HANDLE *handles=NULL;UINTN count=0,intel=0;BOOLEAN valid=TRUE;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION physical={0},forwarded={0};BOOLEAN forwarding=FALSE;
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  GuLog("GOP enumeration: status=%#llx handles=%zu\n",(unsigned long long)status,(size_t)count);
  if(EFI_ERROR(status) || !handles || !count || count>16){if(handles)GuFree(handles);return FALSE;}
  for(UINTN i=0;i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
    status=Gu.bs->HandleProtocol(handles[i],&GopGuid,(VOID **)&gop);
    if(EFI_ERROR(status) || !gop || !gop->Mode){
      GuLog("GOP[%u] invalid protocol/mode: status=%#llx\n",(unsigned)i,
        (unsigned long long)status);valid=FALSE;break;
    }
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode=gop->Mode;
    status=Gu.bs->HandleProtocol(handles[i],&PathGuid,(VOID **)&path);
    int route=0;
    if(!EFI_ERROR(status) && path) {
#if GU_PROFILE_BUILD
      route=GuIndependentDisplay(Gu.bs,Gu.pci,Gu.handle,path)?1:0;
#else
      route=GuDisplayPath(path,256);
#endif
    }
    GuLog("GOP[%u] path_status=%#llx route=%d framebuffer=%#llx bytes=%zu mode=%u max_mode=%u info_bytes=%zu console=%u\n",
      (unsigned)i,(unsigned long long)status,route,(unsigned long long)mode->FrameBufferBase,
      (size_t)mode->FrameBufferSize,mode->Mode,mode->MaxMode,(size_t)mode->SizeOfInfo,(unsigned)(handles[i]==Table->ConsoleOutHandle));
    if((status==EFI_UNSUPPORTED || status==EFI_NOT_FOUND) && !path && !mode->FrameBufferBase &&
       !mode->FrameBufferSize) {
      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
      if(ConsoleGopCheck(gop,i,&info)) {
        if(info.PixelFormat==PixelBltOnly)continue;
        /* EDK2 ConSplitter forwards QueryMode to its sole physical GOP. Accept
           that RGB/BGR result only on the system console, then match it against
           the one validated Intel GOP after enumeration (handle order varies). */
        if(handles[i]==Table->ConsoleOutHandle && !forwarding) {
          forwarded=info;forwarding=TRUE;continue;
        }
      }
      valid=FALSE;break;
    }
    if(!mode->Info || mode->SizeOfInfo<sizeof(*mode->Info)){valid=FALSE;break;}
    GopInfoLog(i,mode->Info);
    if(EFI_ERROR(status) || !path || route!=1 || !mode->FrameBufferBase ||
       !mode->FrameBufferSize || mode->Info->PixelFormat>PixelBlueGreenRedReserved8BitPerColor ||
       !mode->Info->HorizontalResolution || !mode->Info->VerticalResolution ||
       mode->Info->PixelsPerScanLine<mode->Info->HorizontalResolution ||
       (UINT64)mode->Info->PixelsPerScanLine*mode->Info->VerticalResolution>mode->FrameBufferSize/4) {
      valid=FALSE;break;
    }
    physical=*mode->Info;intel++;
  }
  if(forwarding) {
    BOOLEAN match=intel==1 && forwarded.Version==physical.Version && forwarded.PixelFormat==physical.PixelFormat &&
      forwarded.HorizontalResolution==physical.HorizontalResolution && forwarded.VerticalResolution==physical.VerticalResolution &&
      forwarded.PixelsPerScanLine==physical.PixelsPerScanLine;
    GuLog("GOP console forwarded Intel match=%u\n",match);valid=valid && match;
  }
  GuFree(handles);GuLog("GOP result: valid=%u intel_physical=%zu\n",valid,(size_t)intel);
  return valid && intel==1 && GuLogReady();
}
static EFI_STATUS ReadConfig(UINT32 *Config) {
  for(UINTN i=0;i<64;i++)if(EFI_ERROR(GuConfigRead(EfiPciIoWidthUint32,(UINT32)i*4,&Config[i]))) {
    GuLog("PCI config read failed: offset=%#x valid_bytes=%u; snapshot incomplete\n",(unsigned)(i*4),(unsigned)(i*4));
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS ConfigCheck(BOOLEAN Save) {
  UINT32 config[64];GU_CAPS caps;UINTN segment,bus,device,function;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  GuLog("target location: status=%#llx\n",(unsigned long long)status);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(config);if(EFI_ERROR(status))return status;
  BOOLEAN checked=GuCheckConfig(config,segment,bus,device,function,&caps);
  BOOLEAN valid=checked && !(config[(caps.pcie+8)/4]&0x00200000U);
#if GU_PROFILE_BUILD
  valid=valid && config[2]==GU_CLASS_REVISION;
#else
  valid=valid && config[2]==0x030000a1 && config[0xc8/4]==0x00050011 && config[0x10/4]==0x84000000;
#endif
  BOOLEAN same=Save || !memcmp(config,InitialConfig,sizeof(config));
  if(Save || !valid || !same)ConfigLog(config);
  GuLog("target BDF=%zu:%zu:%zu.%zu checks=%u pm=%#x pcie=%#x msi=%#x msix=%#x\n",
    (size_t)segment,(size_t)bus,(size_t)device,(size_t)function,checked,
    (unsigned)caps.pm,(unsigned)caps.pcie,(unsigned)caps.msi,(unsigned)caps.msix);
  if(!valid)return EFI_UNSUPPORTED;
  if(!same)return EFI_NOT_READY;
  if(Save)memcpy(InitialConfig,config,sizeof(config));
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  return EFI_SUCCESS;
}
static EFI_STATUS RestoreDecode(VOID) {
  UINT32 current[64],normalized[64],after[64];UINTN segment,bus,device,function;
  if(!Gu.pci || !Gu.owned || !Gu.armed || !Gu.display_ok || Gu.started || Gu.allocations ||
    Gu.dma_count || Gu.fault || Gu.retained)return EFI_ACCESS_DENIED;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(current);if(EFI_ERROR(status))return status;
  GuLogText("config before decode restore (256 valid bytes):\n");ConfigLog(current);
  if(!GuTargetConfig(current,segment,bus,device,function))return EFI_UNSUPPORTED;
  UINT32 missing=InitialConfig[1]&~current[1]&3U;
  memcpy(normalized,current,sizeof(normalized));normalized[1]|=missing;
  /* Restore only cleared, previously enabled IO/MEM bits. Every other bit,
     including BME, status, BAR, ROM and PM/capability state must be unchanged. */
  if(memcmp(normalized,InitialConfig,sizeof(normalized))) {
    GuLogText("decode restore refused: config differs beyond cleared original IO/MEM bits\n");
    return EFI_UNSUPPORTED;
  }
  if(!missing){GuLogText("decode restore not needed\n");return EFI_SUCCESS;}
  if(!Gu.pci->Attributes)return EFI_UNSUPPORTED;
  UINT64 needed=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  UINT64 supported=0,attributes=0;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationSupported,0,&supported);
  GuLog("decode supported attributes: status=%#llx mask=%#llx\n",(unsigned long long)status,(unsigned long long)supported);
  if(EFI_ERROR(status))return status;
  if((supported&needed)!=needed)return EFI_UNSUPPORTED;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes);
  GuLog("decode current attributes: status=%#llx mask=%#llx\n",(unsigned long long)status,(unsigned long long)attributes);
  if(EFI_ERROR(status))return status;
  GuLog("decode restore intent: saved_command=%#x current_command=%#x missing=%#x attributes=%#llx\n",
    InitialConfig[1]&0xffff,current[1]&0xffff,missing,(unsigned long long)needed);
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
  /* Never use EFI_PCI_DEVICE_ENABLE: it would also enable bus mastering. */
  EFI_STATUS enabled=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationEnable,needed,NULL);
  GuLog("decode enable status=%#llx\n",(unsigned long long)enabled);
  if(!GuLogReady() || Gu.fault)return EFI_ERROR(enabled)?enabled:EFI_DEVICE_ERROR;
  /* Even an error may follow a partial hardware effect. Capture it if readable,
     but never turn an API error into success merely because readback matches. */
  status=ReadConfig(after);
  BOOLEAN matched=FALSE;
  if(!EFI_ERROR(status)) {
    GuLogText("config after decode restore (256 valid bytes):\n");ConfigLog(after);
    matched=!memcmp(after,InitialConfig,sizeof(after));GuLog("decode readback matched=%u\n",matched);
  }
  if(EFI_ERROR(enabled))return enabled;
  if(EFI_ERROR(status))return status;
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
  return matched?EFI_SUCCESS:EFI_COMPROMISED_DATA;
}
static EFI_STATUS FindTarget(VOID) {
  EFI_HANDLE *handles=NULL;UINTN count=0,found=0;
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&handles);
  GuLog("PCI enumeration: status=%#llx handles=%zu\n",(unsigned long long)status,(size_t)count);
  if(EFI_ERROR(status) || !handles || !count || count>256){if(handles)GuFree(handles);return EFI_NOT_FOUND;}
  for(UINTN i=0;i<count;i++) {
    EFI_PCI_IO_PROTOCOL *pci=NULL;UINT32 id=0;
    status=Gu.bs->HandleProtocol(handles[i],&PciGuid,(VOID **)&pci);
    if(!EFI_ERROR(status) && pci)status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,1,&id);
    if(EFI_ERROR(status) || !pci){GuLog("PCI scan failed: index=%zu status=%#llx\n",(size_t)i,(unsigned long long)status);status=EFI_DEVICE_ERROR;break;}
    if(id==GU_BOARD_ID){
#if GU_PROFILE_BUILD
      UINT32 config[64];UINTN segment,bus,device,function;
      if(EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint32,0,64,config)) ||
         EFI_ERROR(pci->GetLocation(pci,&segment,&bus,&device,&function))) {status=EFI_DEVICE_ERROR;break;}
      if(!GuTargetConfig(config,segment,bus,device,function))continue;
#endif
      found++;Gu.handle=handles[i];Gu.pci=pci;
    }
  }
  GuFree(handles);GuLog("PCI target matches=%zu status=%#llx\n",(size_t)found,(unsigned long long)status);
  if(EFI_ERROR(status) || found!=1)return EFI_UNSUPPORTED;
  return ConfigCheck(TRUE);
}
static EFI_STATUS BarCheck(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *r=NULL;
  EFI_STATUS status=Gu.pci->GetBarAttributes(Gu.pci,0,NULL,(VOID **)&r);
  GuLog("bar0 query: status=%#llx\n",(unsigned long long)status);
  if(!EFI_ERROR(status)) {
    if(r && r->Desc==ACPI_ADDRESS_SPACE_DESCRIPTOR && r->Len==sizeof(*r)-3)
      GuLog("bar0 descriptor: type=%u granularity=%llu min=%#llx max=%#llx length=%#llx translation=%#llx\n",
        (unsigned)r->ResType,(unsigned long long)r->AddrSpaceGranularity,(unsigned long long)r->AddrRangeMin,
        (unsigned long long)r->AddrRangeMax,(unsigned long long)r->AddrLen,(unsigned long long)r->AddrTranslationOffset);
    UINT64 base=0x84000000ULL;
#if GU_PROFILE_BUILD
    UINT32 low=InitialConfig[0x10/4];
    if((low&1) || ((low>>1)&3)==1 || ((low>>1)&3)==3) {if(r)GuFree(r);return EFI_UNSUPPORTED;}
    base=(UINT64)(low&~15U);
    if((low&6)==4)base|=(UINT64)InitialConfig[0x14/4]<<32;
#endif
    if(!r || r->Desc!=ACPI_ADDRESS_SPACE_DESCRIPTOR || r->Len!=sizeof(*r)-3 || r->ResType!=0 ||
      !base || (base&(GU_BAR_SIZE-1)) || base>MAX_UINT64-(GU_BAR_SIZE-1) ||
      r->AddrRangeMin!=base || r->AddrLen!=GU_BAR_SIZE || r->AddrTranslationOffset ||
      /* UEFI describes an inclusive end; pinned EDK2 PciBusDxe instead returns
         PciBar.Alignment. Both representations describe this exact 16 MiB BAR. */
      (r->AddrRangeMax!=base+GU_BAR_SIZE-1 && r->AddrRangeMax!=GU_BAR_SIZE-1) ||
      (r->AddrSpaceGranularity!=32 && (!GU_PROFILE_BUILD || r->AddrSpaceGranularity!=64)) ||
      ((UINT8 *)(r+1))[0]!=ACPI_END_TAG_DESCRIPTOR)status=EFI_UNSUPPORTED;
    else {
      UINT8 *raw=(UINT8 *)r,sum=0;
      for(UINTN i=0;i<sizeof(*r)+2;i++)sum=(UINT8)(sum+raw[i]);
      if(raw[sizeof(*r)+1] && sum)status=EFI_COMPROMISED_DATA;
      else {Gu.bar_base=r->AddrRangeMin;Gu.bar_size=r->AddrLen;}
    }
  }
  if(r)GuFree(r);return status;
}
static EFI_STATUS ColdCheck(VOID) {
  UINT32 chip=GuRead32(0);GuLog("chip: actual=%#x expected=%#x\n",chip,GU_BOARD_CHIP);
  if(chip!=GU_BOARD_CHIP)return EFI_UNSUPPORTED;
#if GU_PROFILE_BUILD
  if(!GpuGa10xPciCandidate(GU_BOARD_ID) || !GpuGa10xChip(chip))return EFI_UNSUPPORTED;
#endif
  struct gpu_unlock_gfw_sample s={.config_ok=true,.plm=GuRead32(GU_GFW_PLM_OFFSET),
    .boot=GuRead32(GU_GFW_BOOT_OFFSET),.cpuctl=GuRead32(GU_GFW_CPUCTL_OFFSET)};
  UINT32 hi=GuRead32(0x1fa828),plm=GuRead32(GU_FEAT_PLM_OFFSET),ss0=GuRead32(GU_FEAT_SS0_OFFSET),ss1=GuRead32(GU_FEAT_SS1_OFFSET);
  GuLog("cold state: chip=%#x plm=%#x gfw=%#x/%#x/%#x WPR2_HI=%#x SS0=%#x SS1=%#x\n",
    GU_BOARD_CHIP,plm,s.plm,s.boot,s.cpuctl,hi,ss0,ss1);
  if(Gu.fault || gpu_unlock_gfw_assess(&s) || hi>>4 || plm!=GU_INITIAL_PLM ||
    ss0!=GU_INITIAL_SS0 || (GU_PROFILE_BUILD ? ss1!=GU_INITIAL_SS1 : (ss1!=2 && ss1!=3)) ||
    GuRead32(0x1183a4)!=GU_FB_MIB)return EFI_NOT_READY;
#if GU_PROFILE_BUILD
  const UINT32 offsets[]=GU_LAYOUT_OFFSETS,values[]=GU_LAYOUT_VALUES;
  for(UINTN i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
    if(GuRead32(offsets[i])!=values[i])return EFI_NOT_READY;
#endif
  return Gu.fault?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS PromCheck(VOID) {
  VOID *prom=GuAlloc(1048576);if(!prom)return EFI_OUT_OF_RESOURCES;
  GuCopyFromIo(prom,(VOID *)(UINTN)(Gu.bar_base+0x300000),1048576);
  BOOLEAN good=!Gu.fault && GuHashMatches(prom,1048576,GU_PROM_SHA256);
  GuFree(prom);GuLog("live PROM hash matched=%u\n",(unsigned)good);return good?EFI_SUCCESS:EFI_CRC_ERROR;
}
static EFI_STATUS Arm(VOID) {
  if(!Table->ConIn || !Table->ConIn->Reset || !Table->ConIn->ReadKeyStroke)return EFI_UNSUPPORTED;
  EFI_STATUS status=Table->ConIn->Reset(Table->ConIn,FALSE);if(EFI_ERROR(status))return status;
#if GU_PROFILE_BUILD
  GuLog("EXPERIMENTAL RTX30 GA10x: profile PCI=%#x subsystem=%#x FB=%u MiB. V67/firmware/FLR compatibility unverified.\n",
    GU_BOARD_ID,GU_BOARD_SUBSYSTEM,GU_FB_MIB);
  GuLogText("READY: independent display / captured PROM and cold state / verified USB log and firmware.\n");
#else
  GuLogText("READY: MSI RTX 3080 10GB / Intel display / verified USB log and firmware.\n");
#endif
  if(PendingOwner.agent)GuLogText("U will release the RTX firmware driver, restore verified access settings if needed, then recheck and attempt once.\n");
  GuLogText("Press U to attempt once; Esc or 30-second timeout cancels.\n");
  if(!GuLogReady())return EFI_DEVICE_ERROR;
  for(UINTN i=0;i<300;i++) {
    EFI_INPUT_KEY key;status=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(!EFI_ERROR(status)) {
      if(key.ScanCode==0x17 || key.UnicodeChar==27)return EFI_ABORTED;
      if(key.UnicodeChar=='u' || key.UnicodeChar=='U')return EFI_SUCCESS;
    } else if(status!=EFI_NOT_READY)return status;
    GuStall(100000);
  }
  return EFI_TIMEOUT;
}
static VOID Halt(VOID) {
  Gu.retained=TRUE;
  GuLogText("STOP: cleanup cannot be proven. Resources remain reserved.\nPOWER OFF using the physical power button; do not continue to Windows.\n");
  /* No ResetSystem, ExitBootServices, retry, protocol close or memory release. */
  for(;;)Gu.bs->Stall(1000000);
}
static VOID LogContainment(VOID) {
  /* Called only after any TPL_NOTIFY containment section has ended. */
  GuLog("BME containment: attribute_status=%#llx hardware_status=%#llx command=%#x cached=%#llx pending=%u\n",
    (unsigned long long)Gu.master_attribute_status,(unsigned long long)Gu.master_hardware_status,
    Gu.master_last_command,(unsigned long long)Gu.master_last_attributes,Gu.master_attributes_pending);
}
static VOID ReleaseHalt(EFI_STATUS Status,BOOLEAN Attempted) {
  /* A failed claim may follow interface removal/replacement during logging.
     Resolve, validate and clear BME without a callback window or stale access. */
  EFI_TPL old=Gu.bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS refreshed=GuRefreshTargetPci();
  EFI_STATUS contained=EFI_ERROR(refreshed)?refreshed:GuClearMaster();
  Gu.bs->RestoreTPL(old);
  LogContainment();
  GuLog("FINAL status=%#llx stage=%s attempted=%u driver_release=1 clean=0 containment=%#llx pci_available=%u\n",
    (unsigned long long)Status,Stage,Attempted,(unsigned long long)contained,(unsigned)(Gu.pci!=NULL));
  Halt();
}
#ifdef GU_GOP_EXPERIMENT
#include "../experiments/gop/patch.h"
#include "../experiments/gop/visual.h"
static BOOLEAN PatchIo;
static GC_CONTEXT *PhaseContext;
BOOLEAN GuPhase(const char *phase){
  if(!GvMark(PhaseContext,phase)){Gu.fault=TRUE;return FALSE;}
  return TRUE;
}
EFI_STATUS GxPrepare(GC_CONTEXT *c) {
  Table=c->table;GuInit(Table->BootServices,c->pci);Gu.image=c->image;Gu.handle=c->target;
  PhaseContext=c;EFI_STATUS clock=GvClockInit(c);if(EFI_ERROR(clock))return clock;
  if(!GuPhase("preflight-begin"))return EFI_DEVICE_ERROR;
  PatchIo=TRUE;GuIoSetQuiet(c->quiet);
  EFI_STATUS s=GuIoOpen(c->image,Table);
  if(!EFI_ERROR(s))s=ConfigCheck(TRUE);
  if(!EFI_ERROR(s))s=BarCheck();
  if(!EFI_ERROR(s))s=ColdCheck();
  if(!EFI_ERROR(s))s=PromCheck();
  if(!EFI_ERROR(s)){
    if(!GuPhase("assets-begin"))s=EFI_DEVICE_ERROR;
    else {s=GuLoadAssets();if(!GuPhase("assets-end"))s=EFI_DEVICE_ERROR;}
  }
  GuLog("PATCH_PREFLIGHT status=%#llx profile=%u pci=%#x subsystem=%#x\n",
    (unsigned long long)s,GU_PROFILE_BUILD,GU_BOARD_ID,GU_BOARD_SUBSYSTEM);
  if(!GuPhase("preflight-end"))return EFI_DEVICE_ERROR;
  return !GuLogReady() && !EFI_ERROR(s)?EFI_DEVICE_ERROR:s;
}
BOOLEAN GxSafe(VOID) {
  return GuSafeToReturn() && !Gu.fault && !Gu.allocations && !Gu.dma_count && !Gu.master_attributes_pending;
}
VOID GxReleased(VOID){Gu.owned=FALSE;Gu.armed=FALSE;Gu.display_ok=FALSE;}
EFI_STATUS GxPatch(BOOLEAN (*isolated)(VOID)) {
  Gu.owned=TRUE;Gu.armed=TRUE;Gu.display_check=isolated;Gu.display_ok=isolated();
  EFI_STATUS s=Gu.display_ok?RestoreDecode():EFI_ACCESS_DENIED;
  if(!EFI_ERROR(s))s=ConfigCheck(FALSE);
  if(!EFI_ERROR(s))s=BarCheck();
  if(!EFI_ERROR(s))s=ColdCheck();
  if(!EFI_ERROR(s))s=PromCheck();
  if(EFI_ERROR(s) || !GuLogReady())return EFI_ERROR(s)?s:EFI_DEVICE_ERROR;
  GuLogText("PATCH_INTENT: one real unlock transaction including its own FLR; no extra reset\n");
  if(!GuLogReady())return EFI_DEVICE_ERROR;
  if(!GuPhase("transaction-begin"))return EFI_DEVICE_ERROR;
  UINT64 mask=0xffffffffULL;
  struct pci_dev device={.dev={.dma_mask=&mask,.coherent_dma_mask=0xffffffffULL},.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  int result=gpu_unlock_610_load_shared(&device,&shared);
  if(!result)result=gpu_unlock_610_unlock(&device,&shared);
  EFI_STATUS contained=GuClearMaster();LogContainment();
  GuLog("PATCH_TRANSACTION result=%d containment=%#llx started=%u quiesced=%u fault=%u retained=%u maps=%u FLR=%u safe=%u\n",
    result,(unsigned long long)contained,Gu.started,Gu.quiesced,Gu.fault,Gu.retained,Gu.dma_count,Gu.flrs,GxSafe());
  if(!GxSafe() || EFI_ERROR(contained))return EFI_DEVICE_ERROR;
  gpu_unlock_610_release_shared(&shared);
  if(!GuPhase("transaction-end"))return EFI_DEVICE_ERROR;
  return result || Gu.flrs!=1 || !GuLogReady()?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
EFI_STATUS GxObserve(GC_CONTEXT *c,const char *phase) {
  UINT32 id=0;UINT16 command=0xffff;
  EFI_STATUS s=c->pci->Pci.Read(c->pci,EfiPciIoWidthUint32,0,1,&id);
  if(!EFI_ERROR(s))s=c->pci->Pci.Read(c->pci,EfiPciIoWidthUint16,4,1,&command);
  if(EFI_ERROR(s) || id!=GU_BOARD_ID || command==0xffff || (command&4) || !(command&2))return EFI_DEVICE_ERROR;
  UINT32 ss0=GuRead32(GU_FEAT_SS0_OFFSET),ss1=GuRead32(GU_FEAT_SS1_OFFSET),plm=GuRead32(GU_FEAT_PLM_OFFSET);
  BOOLEAN match=!Gu.fault && ss0==0x88888888U && ss1==8;
  if(!c->log("PATCH_STATE phase=%s SS0=%08x SS1=%08x PLM=%08x command=%#x read_ok=%u matched=%u\n",
    phase,ss0,ss1,plm,command,!Gu.fault,match))return EFI_DEVICE_ERROR;
  GuLog("PATCH_STATE phase=%s SS0=%08x SS1=%08x PLM=%08x matched=%u\n",phase,ss0,ss1,plm,match);
  return !GuLogReady() || Gu.fault?EFI_DEVICE_ERROR:match?EFI_SUCCESS:EFI_COMPROMISED_DATA;
}
EFI_STATUS GxFinish(VOID) {
  if(!PatchIo)return EFI_SUCCESS;
  if(!GxSafe())return EFI_ACCESS_DENIED;
  GuReleaseAssets();PatchIo=FALSE;return GuIoClose();
}
#else
EFI_STATUS EFIAPI efi_main(EFI_HANDLE Image,EFI_SYSTEM_TABLE *St) {
  if(!St || !St->BootServices || !St->RuntimeServices)return EFI_INVALID_PARAMETER;
  Table=St;GuInit(St->BootServices,NULL);Gu.image=Image;Gu.display_check=DisplayCheck;
  memset(&PendingOwner,0,sizeof(PendingOwner));
  Stage="usb-log";EFI_STATUS status=GuIoOpen(Image,St);BOOLEAN attempted=FALSE,release_attempted=FALSE;
  if(EFI_ERROR(status))goto finish;
  UINT8 secure=0xff;UINTN bytes=1;
  Stage="secure-boot";
  status=St->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,NULL,&bytes,&secure);
  if(EFI_ERROR(status) || bytes!=1 || secure){status=EFI_SECURITY_VIOLATION;goto finish;}
  status=Step(status);if(EFI_ERROR(status))goto finish;
  Stage="watchdog";status=Step(Gu.bs->SetWatchdogTimer(0,0,0,NULL));if(EFI_ERROR(status))goto finish;
  Stage="target";status=Step(FindTarget());if(EFI_ERROR(status))goto finish;
  Stage="display";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
  Stage="ownership";status=GuClaimController();
  if(status==EFI_ACCESS_DENIED) {
    status=GuFindReleaseOwner(St,&PendingOwner,TRUE);
    if(!EFI_ERROR(status))GuLogText("Exclusive ownership deferred until manual U and explicit driver release.\n");
  }
  status=Step(status);if(EFI_ERROR(status))goto finish;
  Stage="config-recheck";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
  Stage="bar0";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
  Stage="cold-state";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  Stage="prom";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  Stage="assets";status=Step(GuLoadAssets());if(EFI_ERROR(status))goto finish;
  Stage="arming";status=Step(Arm());if(EFI_ERROR(status))goto finish;
  Stage="armed-config";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
  Stage="armed-display";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
  Stage="armed-cold-state";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  if(PendingOwner.agent) {
    GU_RELEASE_OWNER current;
    Stage="owner-recheck";
    GuLogText("Manual U confirmed: release RTX firmware driver once, then revalidate.\n");
    if(!GuLogReady()){status=EFI_DEVICE_ERROR;goto finish;}
    /* Durable intent precedes the last owner snapshot. No file IO between a
       successful final comparison and DisconnectController. */
    status=GuFindReleaseOwner(St,&current,FALSE);
    if(!EFI_ERROR(status) && !GuSameReleaseOwner(&PendingOwner,&current))status=EFI_ABORTED;
    if(EFI_ERROR(status)){status=Step(status);goto finish;}
    EFI_PCI_IO_PROTOCOL *prior=Gu.pci;
    Stage="driver-release";release_attempted=TRUE;Gu.armed=TRUE;Gu.pci=NULL;
    EFI_STATUS released=Gu.bs->DisconnectController(Gu.handle,current.agent,NULL);
    EFI_TPL refresh_tpl=Gu.bs->RaiseTPL(TPL_NOTIFY);
    EFI_STATUS refreshed=GuRefreshTargetPci();
    Gu.bs->RestoreTPL(refresh_tpl);
    GuLog("driver release: status=%#llx refresh=%#llx same_pci=%u\n",
      (unsigned long long)released,(unsigned long long)refreshed,(unsigned)(Gu.pci==prior));
    status=Step(EFI_ERROR(released)?released:EFI_ERROR(refreshed)?refreshed:Gu.pci!=prior?EFI_COMPROMISED_DATA:EFI_SUCCESS);
    if(EFI_ERROR(status))goto finish;
    Stage="ownership-after-release";status=Step(GuClaimController());if(EFI_ERROR(status))goto finish;
    Stage="display-after-release";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
    Stage="decode-after-release";status=Step(RestoreDecode());if(EFI_ERROR(status))goto finish;
    Stage="config-after-release";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
    Stage="bar0-after-release";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
    Stage="cold-after-release";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
    Stage="prom-after-release";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  }
  GuLogText("ARMED: one transaction is starting. Do not reset or remove the USB.\n");
  if(!GuLogReady()){status=EFI_DEVICE_ERROR;goto finish;}
  Stage="transaction";Gu.armed=TRUE;attempted=TRUE;
  UINT64 mask=0xffffffffULL;
  struct pci_dev device={.dev={.dma_mask=&mask,.coherent_dma_mask=0xffffffffULL},.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  int result=gpu_unlock_610_load_shared(&device,&shared);
  if(!result)result=gpu_unlock_610_unlock(&device,&shared);
  /* Also covers failures before GuStarted, including BME-enable readback. */
  EFI_STATUS contained=GuClearMaster();
  LogContainment();
  GuLog("transaction result=%d containment=%#llx started=%u quiesced=%u fault=%u retained=%u maps=%u FLR=%u\n",
    result,(unsigned long long)contained,Gu.started,Gu.quiesced,Gu.fault,Gu.retained,Gu.dma_count,Gu.flrs);
  if(EFI_ERROR(contained) || !GuSafeToReturn() || Gu.allocations || (Gu.started && result))Halt();
  gpu_unlock_610_release_shared(&shared);Gu.armed=FALSE;
  status=result?EFI_DEVICE_ERROR:EFI_SUCCESS;
finish:
  if(release_attempted && (EFI_ERROR(status) || Gu.fault))ReleaseHalt(status,attempted);
  if(attempted && !GuSafeToReturn())Halt();
  if(GuLogReady())GuLog("FINAL status=%#llx stage=%s attempted=%u driver_release=%u clean=%u\n",(unsigned long long)status,Stage,attempted,release_attempted,GuSafeToReturn());
  else GuConsole("Preflight or logging failed; no new attempt will start.\n");
  if(!GuLogReady()){status=EFI_DEVICE_ERROR;if(release_attempted)ReleaseHalt(status,attempted);if(Gu.started)Halt();}
  if(attempted && !GuSafeToReturn())Halt();
  EFI_STATUS closed=GuIoClose();
  if(EFI_ERROR(closed)){if(release_attempted)ReleaseHalt(closed,attempted);if(Gu.started)Halt();status=closed;}
  if(Gu.owned) {
    closed=Gu.bs->CloseProtocol(Gu.handle,&PciGuid,Image,NULL);
    if(EFI_ERROR(closed)){if(release_attempted)ReleaseHalt(closed,attempted);if(Gu.started)Halt();status=closed;}else Gu.owned=FALSE;
  }
  GuReleaseAssets();
  if(!EFI_ERROR(status))GuConsole("UEFI transaction completed and cleaned up. Returning to firmware; Windows validation is pending.\n");
  else GuConsole("Attempt cancelled or rejected. Returning without another attempt.\n");
  return status;
}
#endif
