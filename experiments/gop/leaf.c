#include "leaf.h"
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextIn.h>
#include <string.h>
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,
  GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,
  BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,
  InputGuid=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
static BOOLEAN Eq(const EFI_GUID *a,const EFI_GUID *b){return !memcmp(a,b,sizeof(*a));}
static EFI_STATUS Free(GC_CONTEXT *c,VOID *p,EFI_STATUS s){return p && EFI_ERROR(c->table->BootServices->FreePool(p))?EFI_DEVICE_ERROR:s;}
static BOOLEAN Passive(UINT32 a){return a==EFI_OPEN_PROTOCOL_GET_PROTOCOL || a==EFI_OPEN_PROTOCOL_TEST_PROTOCOL || a==EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL;}
static EFI_STATUS Opens(GC_CONTEXT *c,EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **entries,UINTN *n){
  *entries=NULL;*n=0;EFI_STATUS s=c->table->BootServices->OpenProtocolInformation(h,g,entries,n);
  if(!EFI_ERROR(s) && (*n>128 || (*n && !*entries)))s=EFI_COMPROMISED_DATA;
  return s;
}
/* The approved edge is one BY_DRIVER open plus one BY_CHILD open on this protocol. */
EFI_STATUS GcLeafPair(GC_CONTEXT *c,EFI_HANDLE h,EFI_GUID *g,EFI_HANDLE *agent,EFI_HANDLE *child){
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN n=0,owners=0,children=0;
  EFI_STATUS s=Opens(c,h,g,&entries,&n);EFI_HANDLE child_agent=NULL;
  *agent=*child=NULL;
  for(UINTN i=0;!EFI_ERROR(s) && i<n;i++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[i];if(Passive(e->Attributes))continue;
    if(e->OpenCount!=1 || !e->AgentHandle){s=EFI_ACCESS_DENIED;break;}
    if(e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->ControllerHandle==h){*agent=e->AgentHandle;owners++;}
    else if(e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e->ControllerHandle && e->ControllerHandle!=h){*child=e->ControllerHandle;child_agent=e->AgentHandle;children++;}
    else s=EFI_ACCESS_DENIED;
  }
  if(!EFI_ERROR(s) && (owners!=1 || children!=1 || *agent!=child_agent))s=EFI_ACCESS_DENIED;
  return Free(c,entries,s);
}
static EFI_STATUS Binding(GC_CONTEXT *c,EFI_HANDLE agent,GC_DRIVER *d,EFI_DRIVER_BINDING_PROTOCOL **binding){
  EFI_BOOT_SERVICES *bs=c->table->BootServices;EFI_LOADED_IMAGE_PROTOCOL *image=NULL;
  EFI_STATUS s=bs->HandleProtocol(agent,&BindingGuid,(VOID **)binding);
  if(EFI_ERROR(s) || !*binding || (*binding)->DriverBindingHandle!=agent || !(*binding)->Supported || !(*binding)->Stop || !(*binding)->Start ||
    !(*binding)->ImageHandle || (*binding)->ImageHandle==c->image)return EFI_UNSUPPORTED;
  s=bs->HandleProtocol((*binding)->ImageHandle,&LoadedGuid,(VOID **)&image);
  if(EFI_ERROR(s) || !image || image->ImageCodeType!=EfiBootServicesCode || !image->ImageBase ||
    !image->ImageSize || image->ImageSize>64U*1024U*1024U)return EFI_UNSUPPORTED;
  memset(d,0,sizeof(*d));d->handle=agent;d->image=(*binding)->ImageHandle;d->base=image->ImageBase;d->bytes=image->ImageSize;
  d->start=(*binding)->Start;d->stop=(*binding)->Stop;d->version=(*binding)->Version;return EFI_SUCCESS;
}
static EFI_STATUS Add(GL_PLAN *p,EFI_HANDLE h){
  for(UINTN i=0;i<p->handle_count;i++)if(p->handles[i]==h)return EFI_SUCCESS;
  if(!h || p->handle_count==GL_HANDLES)return EFI_UNSUPPORTED;
  p->handles[p->handle_count++]=h;return EFI_SUCCESS;
}
EFI_STATUS GcLeafGuard(GC_CONTEXT *c,const GL_PLAN *p,GL_GUARD *guard,BOOLEAN shared_only){
  EFI_BOOT_SERVICES *bs=c->table->BootServices;EFI_SYSTEM_TABLE *t=c->table;
  memset(guard,0,sizeof(*guard));
  if(t->ConsoleInHandle!=p->console_in || t->ConsoleOutHandle!=p->console_out || t->StandardErrorHandle!=p->stderr_handle ||
    t->ConIn!=p->input || t->ConOut!=p->output || t->StdErr!=p->stderr_protocol)return EFI_COMPROMISED_DATA;
  VOID *interface=NULL;
  EFI_STATUS s=bs->HandleProtocol(c->target,&PciGuid,&interface);
  if(EFI_ERROR(s) || interface!=c->pci)return EFI_COMPROMISED_DATA;
  s=bs->HandleProtocol(p->console_in,&InputGuid,&interface);
  if(EFI_ERROR(s) || interface!=p->input)return EFI_COMPROMISED_DATA;
  s=bs->HandleProtocol(p->console_out,&TextGuid,&interface);
  if(EFI_ERROR(s) || interface!=p->output)return EFI_COMPROMISED_DATA;
  if(p->stderr_handle && p->stderr_protocol) {
    s=bs->HandleProtocol(p->stderr_handle,&TextGuid,&interface);
    if(EFI_ERROR(s) || interface!=p->stderr_protocol)return EFI_COMPROMISED_DATA;
  }
  if(!shared_only) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text=NULL;
  s=bs->HandleProtocol(p->physical,&GopGuid,(VOID **)&gop);
  if(EFI_ERROR(s) || gop!=p->gop || !gop->Mode || !gop->Mode->Info || gop->Mode->SizeOfInfo<sizeof(p->info) ||
    gop->Mode->FrameBufferBase!=p->framebuffer || gop->Mode->FrameBufferSize!=p->framebuffer_size ||
    gop->Mode->Mode!=p->mode || memcmp(gop->Mode->Info,&p->info,sizeof(p->info)))return EFI_COMPROMISED_DATA;
  s=bs->HandleProtocol(p->text,&TextGuid,(VOID **)&text);
  if(EFI_ERROR(s) || text!=p->text_protocol)return EFI_COMPROMISED_DATA;
  }
  for(UINTN k=0;k<p->handle_count;k++) {
    if(shared_only && p->handles[k]!=p->console_in && p->handles[k]!=p->console_out && p->handles[k]!=p->stderr_handle)continue;
    EFI_GUID **protocols=NULL;UINTN count=0;
    s=bs->ProtocolsPerHandle(p->handles[k],&protocols,&count);
    if(!EFI_ERROR(s) && (!protocols || !count || count>64))s=EFI_COMPROMISED_DATA;
    for(UINTN i=0;!EFI_ERROR(s) && i<count;i++) {
      if(!protocols[i]){s=EFI_COMPROMISED_DATA;break;}
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN n=0;
      s=Opens(c,p->handles[k],protocols[i],&entries,&n);
      for(UINTN j=0;!EFI_ERROR(s) && j<n;j++) {
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[j];if(Passive(e->Attributes))continue;
        if(p->handles[k]==p->text && Eq(protocols[i],&TextGuid) && e->AgentHandle==p->driver.handle) {
          UINT32 bit=e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->ControllerHandle==p->text?1:
            e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e->ControllerHandle==p->shared?2:0;
          if(!bit || e->OpenCount!=1 || (guard->member&bit)){s=EFI_ACCESS_DENIED;break;}
          guard->member|=bit;continue;
        }
        if(guard->count==GL_RECORDS){s=EFI_BUFFER_TOO_SMALL;break;}
        GL_RECORD *r=&guard->records[guard->count++];r->handle=p->handles[k];r->protocol=*protocols[i];
        r->agent=e->AgentHandle;r->controller=e->ControllerHandle;r->attributes=e->Attributes;r->count=e->OpenCount;
      }
      s=Free(c,entries,s);
    }
    s=Free(c,protocols,s);if(EFI_ERROR(s))return s;
  }
  return EFI_SUCCESS;
}
BOOLEAN GcLeafSameGuard(const GL_GUARD *a,const GL_GUARD *b){
  if(a->count!=b->count)return FALSE;
  for(UINTN i=0;i<a->count;i++) {
    UINTN matches=0;
    for(UINTN j=0;j<b->count;j++)if(!memcmp(&a->records[i],&b->records[j],sizeof(GL_RECORD)))matches++;
    if(matches!=1)return FALSE;
  }
  return TRUE;
}
EFI_STATUS GcLeafPrepare(GC_CONTEXT *c,GL_PLAN *p){
  memset(p,0,sizeof(*p));EFI_BOOT_SERVICES *bs=c->table->BootServices;
  EFI_HANDLE *handles=NULL;UINTN count=0,physical=0;
  EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  if(!EFI_ERROR(s) && (!handles || !count || count>16))s=EFI_UNSUPPORTED;
  for(UINTN i=0;!EFI_ERROR(s) && i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *path=NULL;EFI_HANDLE resolved=NULL;EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;
    EFI_STATUS found=bs->HandleProtocol(handles[i],&PathGuid,(VOID **)&path);
    if(found==EFI_UNSUPPORTED || found==EFI_NOT_FOUND)continue;
    if(EFI_ERROR(found) || !path || EFI_ERROR(bs->LocateDevicePath(&PciGuid,&path,&resolved)) || resolved!=c->target){s=EFI_UNSUPPORTED;break;}
    found=bs->HandleProtocol(handles[i],&GopGuid,(VOID **)&gop);
    if(EFI_ERROR(found) || !gop || !gop->Mode || !gop->Mode->Info || gop->Mode->SizeOfInfo<sizeof(p->info) ||
      !gop->Mode->FrameBufferBase || !gop->Mode->FrameBufferSize){s=EFI_UNSUPPORTED;break;}
    physical++;p->physical=handles[i];p->gop=gop;p->info=*gop->Mode->Info;
    p->framebuffer=gop->Mode->FrameBufferBase;p->framebuffer_size=gop->Mode->FrameBufferSize;p->mode=gop->Mode->Mode;
  }
  s=Free(c,handles,s);if(EFI_ERROR(s))return s;if(physical!=1)return EFI_UNSUPPORTED;
  EFI_HANDLE bridge=NULL;
  s=GcLeafPair(c,p->physical,&GopGuid,&bridge,&p->text);if(EFI_ERROR(s))return s;
  EFI_HANDLE agent=NULL;s=GcLeafPair(c,p->text,&TextGuid,&agent,&p->shared);if(EFI_ERROR(s))return s;
  if(p->shared!=c->table->ConsoleOutHandle || p->text==p->physical || p->text==c->target ||
    p->text==p->shared || !agent || agent==bridge || agent==c->image)return EFI_UNSUPPORTED;
  EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;s=Binding(c,agent,&p->driver,&binding);if(EFI_ERROR(s))return s;
  p->supported=binding->Supported;
  s=bs->HandleProtocol(p->text,&TextGuid,(VOID **)&p->text_protocol);
  if(EFI_ERROR(s) || !p->text_protocol || !p->text_protocol->OutputString)return EFI_UNSUPPORTED;
  p->console_in=c->table->ConsoleInHandle;p->console_out=c->table->ConsoleOutHandle;p->stderr_handle=c->table->StandardErrorHandle;
  p->input=c->table->ConIn;p->output=c->table->ConOut;p->stderr_protocol=c->table->StdErr;
  if(!p->input || !p->output)return EFI_UNSUPPORTED;
  s=Add(p,c->target);if(!EFI_ERROR(s))s=Add(p,p->physical);if(!EFI_ERROR(s))s=Add(p,p->text);
  if(!EFI_ERROR(s))s=Add(p,p->shared);if(!EFI_ERROR(s))s=Add(p,p->console_in);
  if(!EFI_ERROR(s) && p->stderr_handle)s=Add(p,p->stderr_handle);if(EFI_ERROR(s))return s;
  /* Include every direct vendor-GOP child, including the EDID-only sibling. */
  EFI_GUID *roots[]={&PciGuid,&PathGuid};
  for(UINTN k=0;k<2;k++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN n=0;s=Opens(c,c->target,roots[k],&entries,&n);
    for(UINTN i=0;!EFI_ERROR(s) && i<n;i++)if(entries[i].Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER)s=Add(p,entries[i].ControllerHandle);
    s=Free(c,entries,s);if(EFI_ERROR(s))return s;
  }
  s=GcLeafGuard(c,p,&p->guard,FALSE);if(EFI_ERROR(s))return s;if(p->guard.member!=3)return EFI_ACCESS_DENIED;
  return c->log("LEAF_READY physical=%#llx text=%#llx shared=%#llx driver=%#llx guarded_handles=%zu records=%zu\n",
    (unsigned long long)(UINTN)p->physical,(unsigned long long)(UINTN)p->text,(unsigned long long)(UINTN)p->shared,
    (unsigned long long)(UINTN)p->driver.handle,(size_t)p->handle_count,(size_t)p->guard.count)?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
EFI_STATUS GcLeafRun(GC_CONTEXT *c,const GL_PLAN *p,GC_RESULT *r){
  memset(r,0,sizeof(*r));r->detach=r->isolated=r->claim=r->close=r->connect=r->verify=EFI_NOT_STARTED;
  GL_GUARD current;GC_DRIVER driver;EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;
  if(!c->log("LEAF_INTENT: Stop of one text member, then same-driver Supported/Start; GPU GOP/EDID retained\n"))return EFI_DEVICE_ERROR;
  EFI_STATUS s=Binding(c,p->driver.handle,&driver,&binding);if(EFI_ERROR(s))return s;
  if(memcmp(&driver,&p->driver,sizeof(driver)) || binding->Supported!=p->supported)return EFI_ABORTED;
  s=GcLeafGuard(c,p,&current,FALSE);if(EFI_ERROR(s))return s;
  if(current.member!=3 || !GcLeafSameGuard(&p->guard,&current))return EFI_ABORTED;
  EFI_HANDLE child=p->shared;r->touched=TRUE;
  /* These exact parent/child opens satisfy DriverBinding.Stop's direct-call requirements. */
  r->detach=binding->Stop(binding,p->text,1,&child);
  r->isolated=GcLeafGuard(c,p,&current,FALSE);
  BOOLEAN unchanged=!EFI_ERROR(r->isolated) && GcLeafSameGuard(&p->guard,&current);
  c->log("LEAF_AFTER_STOP stop=%#llx guard_status=%#llx member=%u guard_unchanged=%u records=%zu\n",
    (unsigned long long)r->detach,(unsigned long long)r->isolated,current.member,unchanged,(size_t)current.count);
  if(!unchanged){r->isolated=EFI_COMPROMISED_DATA;return r->isolated;}
  /* Recovery still runs after a file error; do not call a console. */
  if(current.member!=0)r->isolated=EFI_ACCESS_DENIED;
  if(current.member!=3) {
    s=Binding(c,p->driver.handle,&driver,&binding);
    if(!EFI_ERROR(s) && (memcmp(&driver,&p->driver,sizeof(driver)) || binding->Supported!=p->supported))s=EFI_ABORTED;
    if(!EFI_ERROR(s))s=binding->Supported(binding,p->text,NULL);
    /* Direct Start is legal only after Supported succeeds with identical parameters. */
    r->connect=s==EFI_SUCCESS?binding->Start(binding,p->text,NULL):s;
    c->log("LEAF_RESTORE supported=%#llx start=%#llx\n",(unsigned long long)s,(unsigned long long)r->connect);
  } else r->connect=EFI_SUCCESS;
  r->verify=GcLeafGuard(c,p,&current,FALSE);
  r->restored=!EFI_ERROR(r->connect) && !EFI_ERROR(r->verify) && current.member==3 && GcLeafSameGuard(&p->guard,&current);
  if(!c->log("LEAF_RESULT stop=%#llx isolated=%#llx connect=%#llx verify=%#llx restored=%u\n",
    (unsigned long long)r->detach,(unsigned long long)r->isolated,(unsigned long long)r->connect,
    (unsigned long long)r->verify,r->restored))return EFI_DEVICE_ERROR;
  return !EFI_ERROR(r->detach) && !EFI_ERROR(r->isolated) && r->restored?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
