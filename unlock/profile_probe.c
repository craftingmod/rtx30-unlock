#include "../probe_profile.h"
#include "runtime_profile.h"
#include "sha256.h"

static UINT8 Previous[PROBE_PROFILE_BYTES],Pending[PROBE_PROFILE_BYTES];
static BOOLEAN HavePending;

static EFI_STATUS Capture(EFI_SYSTEM_TABLE *table, EFI_FILE_PROTOCOL *parent,
                          BOOLEAN debug, UINT8 *bytes) {
  EFI_FILE_PROTOCOL *run=NULL,*file=NULL;
  EFI_STATUS status;
  if(!debug)return ProbeCaptureProfile(table,NULL,bytes);
  for(UINTN slot=0;slot<100;slot++) {
    CHAR16 name[]=L"PROBE00";name[5]=(CHAR16)('0'+slot/10);name[6]=(CHAR16)('0'+slot%10);
    file=NULL;status=parent->Open(parent,&file,name,EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)) {
      if(!file)return EFI_DEVICE_ERROR;
      status=file->Close(file);if(EFI_ERROR(status))return status;
      continue;
    }
    if(status!=EFI_NOT_FOUND)return status;
    status=parent->Open(parent,&run,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(EFI_ERROR(status))return status;
    break;
  }
  if(!run)return EFI_VOLUME_FULL;
  status=ProbeCaptureProfile(table,run,bytes);
  EFI_STATUS closed=run->Close(run);
  if(EFI_ERROR(closed))return closed;
  return status;
}

/* A missing profile is created without changing DEBUG.ON or requiring approval. */
EFI_STATUS GuProfileCreate(EFI_SYSTEM_TABLE *table, EFI_FILE_PROTOCOL *parent, BOOLEAN debug) {
  UINT8 bytes[PROBE_PROFILE_BYTES];HavePending=FALSE;
  EFI_STATUS status=Capture(table,parent,debug,bytes);
  if(EFI_ERROR(status))return status;
  status=ProbeSaveNew(parent,L"PROFILE.BIN",bytes,sizeof(bytes));
  if(EFI_ERROR(status))return status;
  /* A short write, close failure or corrupt read never reaches the unlock path. */
  return GuProfileLoad(parent);
}

EFI_STATUS GuProfilePromChanged(EFI_SYSTEM_TABLE *table, EFI_PCI_IO_PROTOCOL *pci, BOOLEAN *changed) {
  static const char hex[]="0123456789abcdef";
  PROBE_RESULT r;VOID *prom=NULL;UINT8 digest[32];
  *changed=FALSE;
  EFI_STATUS status=ProbeCollect(pci,table->BootServices,&r);
  if(EFI_ERROR(status))return status;
  /* A different device or unreadable state must never be accepted as a PROM update. */
  if(r.Config[0]!=GuProfile.identity || r.Config[11]!=GuProfile.subsystem ||
     r.Config[2]!=GuProfile.class_revision || r.Samples[0].Value!=GuProfile.chip ||
     r.BarSize!=GuProfile.bar_size)return EFI_UNSUPPORTED;
  status=table->BootServices->AllocatePool(EfiLoaderData,PROBE_PROM_SIZE,&prom);
  if(EFI_ERROR(status) || !prom)return EFI_OUT_OF_RESOURCES;
  status=ProbeReadProm(pci,&r,prom,PROBE_PROM_SIZE);
  if(!EFI_ERROR(status) && !ProbeRomUsable(prom,PROBE_PROM_SIZE))status=EFI_COMPROMISED_DATA;
  if(!EFI_ERROR(status))GuSha256(prom,PROBE_PROM_SIZE,digest);
  EFI_STATUS closed=table->BootServices->FreePool(prom);
  if(EFI_ERROR(closed))return closed;
  if(EFI_ERROR(status))return status;
  for(UINTN i=0;i<32;i++)
    if(GuProfile.prom_sha256[i*2]!=hex[digest[i]>>4] || GuProfile.prom_sha256[i*2+1]!=hex[digest[i]&15])*changed=TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS GuProfileRefresh(EFI_SYSTEM_TABLE *table, EFI_FILE_PROTOCOL *parent, BOOLEAN debug) {
  HavePending=FALSE;
  EFI_STATUS status=GuProfileRead(parent,Previous);
  if(EFI_ERROR(status))return status;
  GU_RUNTIME_PROFILE old=GuProfile,candidate;
  status=Capture(table,parent,debug,Pending);
  if(EFI_ERROR(status))return status;
  if(!GuProfileDecode(Pending,sizeof(Pending),&candidate) ||
     candidate.identity!=old.identity || candidate.subsystem!=old.subsystem ||
     candidate.chip!=old.chip || candidate.class_revision!=old.class_revision ||
     candidate.bar_size!=old.bar_size)return EFI_UNSUPPORTED;
  /* Use the candidate for all preflight checks; keep the disk profile intact until U. */
  GuProfile=candidate;HavePending=TRUE;return EFI_SUCCESS;
}

EFI_STATUS GuProfileCommit(EFI_FILE_PROTOCOL *parent) {
  if(!HavePending)return EFI_NOT_READY;
  EFI_FILE_PROTOCOL *file=NULL;UINT8 bytes[PROBE_PROFILE_BYTES],extra;
  EFI_STATUS status=parent->Open(parent,&file,L"PROFILE.BIN",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
  if(EFI_ERROR(status))return status;
  if(!file || !file->Close)return EFI_DEVICE_ERROR;
  if(!file->Read || !file->SetPosition || !file->Write || !file->Flush){file->Close(file);return EFI_UNSUPPORTED;}
  UINTN size=sizeof(bytes);status=file->Read(file,&size,bytes);
  if(!EFI_ERROR(status) && size!=sizeof(bytes))status=EFI_END_OF_FILE;
  if(!EFI_ERROR(status)) {
    size=1;status=file->Read(file,&size,&extra);
    if(!EFI_ERROR(status) && size)status=EFI_COMPROMISED_DATA;
  }
  for(UINTN i=0;!EFI_ERROR(status) && i<sizeof(bytes);i++)
    if(bytes[i]!=Previous[i])status=EFI_COMPROMISED_DATA;
  if(!EFI_ERROR(status))status=file->SetPosition(file,0);
  if(EFI_ERROR(status)){file->Close(file);return status;}
  status=ProbeWriteFile(file,Pending,sizeof(Pending)); /* Writes, flushes, rereads and closes. */
  if(EFI_ERROR(status))return status;
  status=GuProfileLoad(parent);
  if(!EFI_ERROR(status))HavePending=FALSE;
  return status;
}
