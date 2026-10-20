#include "full.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/SimpleTextOut.h>
#include <string.h>
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,
  GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,
  EdidGuid=EFI_EDID_DISCOVERED_PROTOCOL_GUID,BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,
  LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static BOOLEAN Eq(const EFI_GUID *a,const EFI_GUID *b){return !memcmp(a,b,sizeof(*a));}
static BOOLEAN Passive(UINT32 a){return a==1 || a==2 || a==4;}
static EFI_STATUS Free(GC_CONTEXT *c,VOID *p,EFI_STATUS s){return p && EFI_ERROR(c->table->BootServices->FreePool(p))?EFI_DEVICE_ERROR:s;}
static EFI_STATUS Binding(GC_CONTEXT *c,EFI_HANDLE agent,BOOLEAN runtime,GF_DRIVER *d,EFI_DRIVER_BINDING_PROTOCOL **binding){
  EFI_BOOT_SERVICES *bs=c->table->BootServices;EFI_LOADED_IMAGE_PROTOCOL *image=NULL;
  EFI_STATUS s=bs->HandleProtocol(agent,&BindingGuid,(VOID **)binding);
  if(EFI_ERROR(s) || !*binding || (*binding)->DriverBindingHandle!=agent || !(*binding)->Supported ||
    !(*binding)->Start || !(*binding)->Stop || !(*binding)->ImageHandle || (*binding)->ImageHandle==c->image)return EFI_UNSUPPORTED;
  s=bs->HandleProtocol((*binding)->ImageHandle,&LoadedGuid,(VOID **)&image);
  if(EFI_ERROR(s) || !image || !image->ImageBase || !image->ImageSize || image->ImageSize>64U*1024U*1024U ||
    (image->ImageCodeType!=EfiBootServicesCode && !(runtime && image->ImageCodeType==EfiRuntimeServicesCode)))return EFI_UNSUPPORTED;
  memset(d,0,sizeof(*d));d->driver.handle=agent;d->driver.image=(*binding)->ImageHandle;
  d->driver.base=image->ImageBase;d->driver.bytes=image->ImageSize;d->driver.start=(*binding)->Start;
  d->driver.stop=(*binding)->Stop;d->driver.version=(*binding)->Version;d->supported=(*binding)->Supported;d->runtime=runtime;return EFI_SUCCESS;
}
static EFI_HANDLE Owner(const GL_GUARD *g,EFI_HANDLE h,const EFI_GUID *protocol){
  EFI_HANDLE owner=NULL;UINTN n=0;
  for(UINTN i=0;i<g->count;i++) {
    const GL_RECORD *r=&g->records[i];
    if(r->handle==h && Eq(&r->protocol,protocol) && r->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller==h && r->count==1){owner=r->agent;n++;}
  }
  return n==1?owner:NULL;
}
static EFI_STATUS Children(GC_CONTEXT *c,EFI_HANDLE agent,EFI_HANDLE *a,EFI_HANDLE *b){
  *a=*b=NULL;EFI_GUID *guids[]={&PciGuid,&PathGuid};
  for(UINTN k=0;k<2;k++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN n=0;
    EFI_STATUS s=c->table->BootServices->OpenProtocolInformation(c->target,guids[k],&entries,&n);
    if(!EFI_ERROR(s) && (n>128 || (n && !entries)))s=EFI_COMPROMISED_DATA;
    for(UINTN i=0;!EFI_ERROR(s) && i<n;i++)if(entries[i].Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
      EFI_HANDLE child=entries[i].ControllerHandle;
      if(entries[i].AgentHandle!=agent || entries[i].OpenCount!=1 || !child || child==c->target){s=EFI_ACCESS_DENIED;break;}
      if(child==*a || child==*b)continue;
      if(!*a)*a=child;else if(!*b)*b=child;else s=EFI_UNSUPPORTED;
    }
    s=Free(c,entries,s);if(EFI_ERROR(s))return s;
  }
  return *a && *b?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS Shared(GC_CONTEXT *c,const GF_PLAN *p){
  GL_GUARD g;EFI_STATUS s=GcLeafGuard(c,&p->leaf,&g,TRUE);
  if(!EFI_ERROR(s) && !GcLeafSameGuard(&p->shared,&g))s=EFI_COMPROMISED_DATA;
  return s;
}
static EFI_STATUS Recheck(GC_CONTEXT *c,const GF_PLAN *p){
  GL_GUARD g;EFI_STATUS s=GcLeafGuard(c,&p->leaf,&g,FALSE);
  if(!EFI_ERROR(s) && (g.member!=3 || !GcLeafSameGuard(&p->leaf.guard,&g)))s=EFI_ABORTED;
  return s;
}
static EFI_STATUS Owned(GC_CONTEXT *c,EFI_HANDLE h,EFI_HANDLE agent,UINTN *drivers,UINTN *children){
  *drivers=*children=0;EFI_GUID **guids=NULL;UINTN n=0;EFI_BOOT_SERVICES *bs=c->table->BootServices;
  EFI_STATUS s=bs->ProtocolsPerHandle(h,&guids,&n);
  if(!EFI_ERROR(s) && (!guids || !n || n>64))s=EFI_COMPROMISED_DATA;
  for(UINTN i=0;!EFI_ERROR(s) && i<n;i++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=NULL;UINTN count=0;
    if(!guids[i]){s=EFI_COMPROMISED_DATA;break;}
    s=bs->OpenProtocolInformation(h,guids[i],&e,&count);
    if(!agent && !c->log("FULL_ROOT_PROTOCOL handle=%#llx guid=%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x status=%#llx entries=%zu\n",
      (unsigned long long)(UINTN)h,guids[i]->Data1,guids[i]->Data2,guids[i]->Data3,guids[i]->Data4[0],guids[i]->Data4[1],
      guids[i]->Data4[2],guids[i]->Data4[3],guids[i]->Data4[4],guids[i]->Data4[5],guids[i]->Data4[6],guids[i]->Data4[7],
      (unsigned long long)s,(size_t)count))s=EFI_DEVICE_ERROR;
    if(!EFI_ERROR(s) && (count>128 || (count && !e)))s=EFI_COMPROMISED_DATA;
    for(UINTN j=0;!EFI_ERROR(s) && j<count;j++)if(!Passive(e[j].Attributes) && (!agent || e[j].AgentHandle==agent)) {
      if(!agent && !c->log("FULL_ROOT_OPEN agent=%#llx controller=%#llx attributes=%#x count=%u\n",
        (unsigned long long)(UINTN)e[j].AgentHandle,(unsigned long long)(UINTN)e[j].ControllerHandle,e[j].Attributes,e[j].OpenCount)){s=EFI_DEVICE_ERROR;break;}
      if(e[j].Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER)(*drivers)++;
      else if(e[j].Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER)(*children)++;
      else s=EFI_ACCESS_DENIED;
    }
    s=Free(c,e,s);
  }
  return Free(c,guids,s);
}
/* Only the observed intermediate aggregate may survive as an ownerless virtual GOP. */
static EFI_STATUS VirtualGop(GC_CONTEXT *c,const GF_PLAN *p,EFI_HANDLE h){
  EFI_BOOT_SERVICES *bs=c->table->BootServices;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  EFI_STATUS gs=bs->HandleProtocol(h,&GopGuid,(VOID **)&gop);
  EFI_STATUS ps=bs->HandleProtocol(h,&PathGuid,(VOID **)&path);
  UINTN owners=0,children=0;EFI_STATUS os=Owned(c,h,NULL,&owners,&children);
  BOOLEAN mode=!EFI_ERROR(gs) && gop && gop->Mode;
  BOOLEAN allowed=h==p->leaf.text && mode && !gop->Mode->FrameBufferBase && !gop->Mode->FrameBufferSize &&
    !path && (ps==EFI_UNSUPPORTED || ps==EFI_NOT_FOUND) && !EFI_ERROR(os) && !owners && !children;
  if(!c->log("FULL_VIRTUAL_GOP handle=%#llx known_intermediate=%u gop_status=%#llx path_status=%#llx ownership_status=%#llx owners=%zu children=%zu mode_valid=%u framebuffer=%#llx bytes=%llu allowed=%u\n",
    (unsigned long long)(UINTN)h,h==p->leaf.text,(unsigned long long)gs,(unsigned long long)ps,(unsigned long long)os,
    (size_t)owners,(size_t)children,mode,mode?(unsigned long long)gop->Mode->FrameBufferBase:0,
    mode?(unsigned long long)gop->Mode->FrameBufferSize:0,allowed))return EFI_DEVICE_ERROR;
  return EFI_ERROR(os)?os:allowed?EFI_SUCCESS:EFI_ACCESS_DENIED;
}
static EFI_STATUS Detached(GC_CONTEXT *c,const GF_PLAN *p){
  EFI_HANDLE old[]={p->leaf.physical,p->leaf.text,p->leaf.text,p->sibling};
  EFI_GUID *guids[]={&GopGuid,&GopGuid,&TextGuid,&EdidGuid};
  const char *roles[]={"physical_gop","intermediate_gop","intermediate_text","sibling_edid"};
  EFI_STATUS result=EFI_SUCCESS;BOOLEAN virtual_old=FALSE,virtual_seen=FALSE;
  for(UINTN i=0;i<4;i++) {
    VOID *interface=NULL;EFI_STATUS s=c->table->BootServices->HandleProtocol(old[i],guids[i],&interface);
    BOOLEAN rejected=interface || (s!=EFI_NOT_FOUND && s!=EFI_UNSUPPORTED && s!=EFI_INVALID_PARAMETER);
    if(i==1 && s==EFI_SUCCESS && interface){
      EFI_STATUS vs=VirtualGop(c,p,old[i]);
      if(vs==EFI_DEVICE_ERROR)return vs;
      rejected=EFI_ERROR(vs);virtual_old=!rejected;
    }
    if(!c->log("FULL_OLD_INTERFACE role=%s handle=%#llx status=%#llx interface=%#llx rejected=%u\n",
      roles[i],(unsigned long long)(UINTN)old[i],(unsigned long long)s,(unsigned long long)(UINTN)interface,rejected))return EFI_DEVICE_ERROR;
    if(rejected)result=EFI_ACCESS_DENIED;
  }
  EFI_HANDLE *handles=NULL;UINTN count=0;
  EFI_STATUS s=c->table->BootServices->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  if(!c->log("FULL_GOP_ENUM status=%#llx count=%zu\n",(unsigned long long)s,(size_t)count))return Free(c,handles,EFI_DEVICE_ERROR);
  if(s==EFI_NOT_FOUND)return Free(c,handles,virtual_old?EFI_COMPROMISED_DATA:result);
  if(!EFI_ERROR(s) && (count>16 || (count && !handles)))s=EFI_COMPROMISED_DATA;
  for(UINTN i=0;!EFI_ERROR(s) && i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *path=NULL;EFI_HANDLE pci=NULL;
    EFI_STATUS path_status=c->table->BootServices->HandleProtocol(handles[i],&PathGuid,(VOID **)&path);
    EFI_STATUS locate=EFI_NOT_STARTED;
    BOOLEAN rejected=FALSE;
    if(!EFI_ERROR(path_status) && path) {
      locate=c->table->BootServices->LocateDevicePath(&PciGuid,&path,&pci);
      rejected=EFI_ERROR(locate) || pci==c->target;
    } else if(path_status!=EFI_NOT_FOUND && path_status!=EFI_UNSUPPORTED)rejected=TRUE;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;
    EFI_STATUS gs=c->table->BootServices->HandleProtocol(handles[i],&GopGuid,(VOID **)&gop);
    BOOLEAN mode_valid=!EFI_ERROR(gs) && gop && gop->Mode;
    if(path_status==EFI_NOT_FOUND || path_status==EFI_UNSUPPORTED){
      EFI_STATUS vs=VirtualGop(c,p,handles[i]);
      if(vs==EFI_DEVICE_ERROR){s=vs;break;}
      rejected=EFI_ERROR(vs);
      if(!rejected && handles[i]==p->leaf.text)virtual_seen=TRUE;
    }
    if(!c->log("FULL_REMAINING_GOP handle=%#llx path_status=%#llx locate_status=%#llx pci=%#llx target=%u gop_status=%#llx mode_valid=%u framebuffer=%#llx bytes=%llu rejected=%u\n",
      (unsigned long long)(UINTN)handles[i],(unsigned long long)path_status,(unsigned long long)locate,
      (unsigned long long)(UINTN)pci,pci==c->target,(unsigned long long)gs,mode_valid,
      mode_valid?(unsigned long long)gop->Mode->FrameBufferBase:0,
      mode_valid?(unsigned long long)gop->Mode->FrameBufferSize:0,rejected)){s=EFI_DEVICE_ERROR;break;}
    if(rejected)result=EFI_ACCESS_DENIED;
  }
  if(!EFI_ERROR(s) && virtual_old && !virtual_seen)s=EFI_COMPROMISED_DATA;
  return Free(c,handles,EFI_ERROR(s)?s:result);
}
EFI_STATUS GfPrepare(GC_CONTEXT *c,GF_PLAN *p){
  memset(p,0,sizeof(*p));EFI_STATUS s=GcLeafPrepare(c,&p->leaf);if(EFI_ERROR(s))return s;
  s=GfSavePci(c,&p->pci);if(EFI_ERROR(s))return s;
  EFI_HANDLE agents[6];agents[0]=Owner(&p->leaf.guard,c->target,&PciGuid);
  agents[1]=Owner(&p->leaf.guard,p->leaf.physical,&GopGuid);
  agents[2]=Owner(&p->leaf.guard,p->leaf.text,&GopGuid);agents[3]=p->leaf.driver.handle;
  EFI_HANDLE a=NULL,b=NULL;s=Children(c,agents[0],&a,&b);if(EFI_ERROR(s))return s;
  if(a!=p->leaf.physical && b!=p->leaf.physical)return EFI_UNSUPPORTED;
  p->sibling=a==p->leaf.physical?b:a;
  if(p->sibling==p->leaf.text || p->sibling==p->leaf.shared)return EFI_UNSUPPORTED;
  agents[4]=Owner(&p->leaf.guard,p->leaf.physical,&EdidGuid);
  agents[5]=Owner(&p->leaf.guard,p->sibling,&EdidGuid);
  for(UINTN i=0;i<6;i++) {
    if(!agents[i] || agents[i]==c->image)return EFI_UNSUPPORTED;
    for(UINTN j=0;j<i;j++)if(agents[i]==agents[j] && !(i==5 && j==4))return EFI_UNSUPPORTED;
    EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;s=Binding(c,agents[i],i>=4,&p->drivers[i],&binding);if(EFI_ERROR(s))return s;
    if(!c->log("FULL_DRIVER role=%u agent=%#llx image=%#llx runtime_allowed=%u\n",(unsigned)i,
      (unsigned long long)(UINTN)agents[i],(unsigned long long)(UINTN)p->drivers[i].driver.image,i>=4))return EFI_DEVICE_ERROR;
  }
  /* Only the observed two-output EDID/GOP topology is authorized for this experiment. */
  for(UINTN i=0;i<p->leaf.guard.count;i++) {
    const GL_RECORD *r=&p->leaf.guard.records[i];BOOLEAN valid=FALSE;
    if(r->handle==p->leaf.console_in || r->handle==p->leaf.console_out || r->handle==p->leaf.stderr_handle)continue;
    if(r->count!=1)return EFI_ACCESS_DENIED;
    if(r->handle==c->target && r->agent==agents[0])valid=
      (Eq(&r->protocol,&PciGuid) && r->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller==c->target) ||
      ((Eq(&r->protocol,&PciGuid) || Eq(&r->protocol,&PathGuid)) && r->attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER &&
        (r->controller==p->leaf.physical || r->controller==p->sibling));
    if(r->handle==p->leaf.physical && Eq(&r->protocol,&GopGuid) && r->agent==agents[1])valid=
      (r->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller==p->leaf.physical) ||
      (r->attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && r->controller==p->leaf.text);
    if(r->handle==p->leaf.physical && Eq(&r->protocol,&EdidGuid) && r->agent==agents[4])valid=r->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller==r->handle;
    if(r->handle==p->sibling && Eq(&r->protocol,&EdidGuid) && r->agent==agents[5])valid=r->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller==r->handle;
    if(r->handle==p->leaf.text && Eq(&r->protocol,&GopGuid) && r->agent==agents[2])valid=r->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller==r->handle;
    if(!valid)return EFI_UNSUPPORTED;
  }
  s=GcLeafGuard(c,&p->leaf,&p->shared,TRUE);if(EFI_ERROR(s))return s;
  return c->log("FULL_READY: two vendor children; all six roles verified; one FLR after full isolation\n")?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static EFI_STATUS DriverNow(GC_CONTEXT *c,const GF_DRIVER *saved,EFI_DRIVER_BINDING_PROTOCOL **binding){
  GF_DRIVER d;EFI_STATUS s=Binding(c,saved->driver.handle,saved->runtime,&d,binding);
  return !EFI_ERROR(s) && memcmp(&d,saved,sizeof(d))?EFI_ABORTED:s;
}
static EFI_STATUS Stop(GC_CONTEXT *c,const GF_PLAN *p,UINTN role,EFI_HANDLE parent,UINTN count,EFI_HANDLE *children){
  EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;EFI_STATUS s=Shared(c,p);if(EFI_ERROR(s))return s;
  s=DriverNow(c,&p->drivers[role],&binding);if(EFI_ERROR(s))return s;
  UINTN owners=0,edges=0;s=Owned(c,parent,p->drivers[role].driver.handle,&owners,&edges);
  if(EFI_ERROR(s) || !owners || (count && edges!=count) || (!count && edges))return EFI_ACCESS_DENIED;
  if(!c->log("FULL_STOP_INTENT role=%u parent=%#llx children=%zu\n",(unsigned)role,(unsigned long long)(UINTN)parent,(size_t)count))return EFI_DEVICE_ERROR;
  s=binding->Stop(binding,parent,count,children);
  EFI_STATUS guard=Shared(c,p);
  c->log("FULL_STOP_RESULT role=%u status=%#llx shared_guard=%#llx\n",(unsigned)role,(unsigned long long)s,(unsigned long long)guard);
  return EFI_ERROR(guard)?guard:s;
}
static EFI_STATUS StopAll(GC_CONTEXT *c,const GF_PLAN *p,UINTN role,EFI_HANDLE parent,UINTN count,EFI_HANDLE *children){
  EFI_STATUS s=Stop(c,p,role,parent,count,children);if(EFI_ERROR(s))return s;
  UINTN owners=0,edges=0;s=Owned(c,parent,p->drivers[role].driver.handle,&owners,&edges);
  if(EFI_ERROR(s) || edges)return EFI_ACCESS_DENIED;
  if(owners){s=Stop(c,p,role,parent,0,NULL);if(EFI_ERROR(s))return s;}
  s=Owned(c,parent,p->drivers[role].driver.handle,&owners,&edges);
  return !EFI_ERROR(s) && (owners || edges)?EFI_ACCESS_DENIED:s;
}
static EFI_STATUS Start(GC_CONTEXT *c,const GF_PLAN *p,UINTN role,EFI_HANDLE parent){
  EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;EFI_STATUS s=Shared(c,p);if(EFI_ERROR(s))return s;
  s=DriverNow(c,&p->drivers[role],&binding);if(EFI_ERROR(s))return s;
  UINTN owners=0,edges=0;s=Owned(c,parent,p->drivers[role].driver.handle,&owners,&edges);if(EFI_ERROR(s))return s;
  if(!owners){s=binding->Supported(binding,parent,NULL);if(s==EFI_SUCCESS)s=binding->Start(binding,parent,NULL);}
  EFI_STATUS guard=Shared(c,p);c->log("FULL_START_RESULT role=%u parent=%#llx status=%#llx shared_guard=%#llx\n",
    (unsigned)role,(unsigned long long)(UINTN)parent,(unsigned long long)s,(unsigned long long)guard);
  return EFI_ERROR(guard)?guard:s;
}
static EFI_HANDLE Map(const GF_PLAN *p,const GL_PLAN *fresh,EFI_HANDLE sibling,EFI_HANDLE h){
  if(h==p->leaf.physical)return fresh->physical;if(h==p->leaf.text)return fresh->text;if(h==p->sibling)return sibling;return h;
}
static EFI_STATUS Restored(GC_CONTEXT *c,const GF_PLAN *p,GL_PLAN *fresh,EFI_HANDLE sibling){
  EFI_STATUS s=GcLeafPrepare(c,fresh);if(EFI_ERROR(s))return s;
  if(memcmp(&fresh->driver,&p->leaf.driver,sizeof(fresh->driver)) || fresh->shared!=p->leaf.shared || fresh->guard.count!=p->leaf.guard.count)return EFI_COMPROMISED_DATA;
  for(UINTN i=0;i<p->leaf.guard.count;i++) {
    GL_RECORD r=p->leaf.guard.records[i];r.handle=Map(p,fresh,sibling,r.handle);r.controller=Map(p,fresh,sibling,r.controller);
    UINTN matches=0;for(UINTN j=0;j<fresh->guard.count;j++)if(!memcmp(&r,&fresh->guard.records[j],sizeof(r)))matches++;
    if(matches!=1)return EFI_COMPROMISED_DATA;
  }
  return Shared(c,p);
}
EFI_STATUS GfRun(GC_CONTEXT *c,const GF_PLAN *p,GC_RESULT *r){
  memset(r,0,sizeof(*r));r->detach=r->isolated=r->claim=r->close=r->connect=r->verify=EFI_NOT_STARTED;
  EFI_BOOT_SERVICES *bs=c->table->BootServices;GL_PLAN *fresh=NULL;
  EFI_STATUS s=bs->AllocatePool(EfiLoaderData,sizeof(*fresh),(VOID **)&fresh);if(EFI_ERROR(s) || !fresh)return EFI_OUT_OF_RESOURCES;
  s=Recheck(c,p);if(EFI_ERROR(s))goto finish;
  /* Recheck PCI before any Stop; no changed BME/interrupt state is accepted after U. */
  GF_PCI now;s=GfSavePci(c,&now);if(EFI_ERROR(s))goto finish;
  if(memcmp(&now,&p->pci,sizeof(now))){s=EFI_ABORTED;goto finish;}
  if(!c->log("FULL_INTENT full GPU GOP release and one FLR; no unlock\n")){s=EFI_DEVICE_ERROR;goto finish;}
  r->touched=TRUE;
  EFI_HANDLE member=p->leaf.shared;
  s=StopAll(c,p,3,p->leaf.text,1,&member);if(EFI_ERROR(s))goto finish;
  s=StopAll(c,p,2,p->leaf.text,0,NULL);if(EFI_ERROR(s))goto finish;
  EFI_HANDLE text_child=p->leaf.text;
  s=StopAll(c,p,1,p->leaf.physical,1,&text_child);if(EFI_ERROR(s))goto finish;
  s=StopAll(c,p,4,p->leaf.physical,0,NULL);if(EFI_ERROR(s))goto finish;
  s=StopAll(c,p,5,p->sibling,0,NULL);if(EFI_ERROR(s))goto finish;
  EFI_HANDLE children[]={p->leaf.physical,p->sibling};
  s=StopAll(c,p,0,c->target,2,children);r->detach=s;if(EFI_ERROR(s))goto finish;
  UINTN owners=0,edges=0;s=Owned(c,c->target,NULL,&owners,&edges);
  if(!EFI_ERROR(s) && (owners || edges))s=EFI_ACCESS_DENIED;
  if(!c->log("FULL_ROOT_SUMMARY status=%#llx owners=%zu children=%zu\n",(unsigned long long)s,(size_t)owners,(size_t)edges)){s=EFI_DEVICE_ERROR;goto finish;}
  /* Collect every bounded residual check even when another isolation check rejects. */
  EFI_STATUS detached=Detached(c,p);
  if(!EFI_ERROR(s) || detached==EFI_DEVICE_ERROR)s=detached;
  r->isolated=s;
  if(!c->log("FULL_ISOLATION_RESULT status=%#llx interfaces=%#llx\n",(unsigned long long)s,(unsigned long long)detached)){s=r->isolated=EFI_DEVICE_ERROR;}
  if(EFI_ERROR(s))goto finish;
  EFI_PCI_IO_PROTOCOL *pci=NULL;r->claim=bs->OpenProtocol(c->target,&PciGuid,(VOID **)&pci,c->image,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
  if(EFI_ERROR(r->claim)){s=r->claim;goto finish;}
  BOOLEAN reset=FALSE;s=pci==c->pci?Shared(c,p):EFI_COMPROMISED_DATA;
  if(!EFI_ERROR(s))s=GfResetPci(c,&p->pci,&reset);
  c->log("FULL_FLR_RESULT attempted=%u status=%#llx\n",reset,(unsigned long long)s);
  r->close=bs->CloseProtocol(c->target,&PciGuid,c->image,NULL);
  if(EFI_ERROR(r->close)){s=r->close;goto finish;}if(EFI_ERROR(s))goto finish;
  s=Start(c,p,0,c->target);if(EFI_ERROR(s))goto finish;
  EFI_HANDLE a=NULL,b=NULL;s=Children(c,p->drivers[0].driver.handle,&a,&b);if(EFI_ERROR(s))goto finish;
  EFI_HANDLE physical=NULL,sibling=NULL;EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;
  EFI_STATUS sa=bs->HandleProtocol(a,&GopGuid,(VOID **)&gop);
  if(!EFI_ERROR(sa) && gop){physical=a;sibling=b;}else {s=bs->HandleProtocol(b,&GopGuid,(VOID **)&gop);if(EFI_ERROR(s) || !gop){s=EFI_NOT_FOUND;goto finish;}physical=b;sibling=a;}
  s=Start(c,p,4,physical);if(EFI_ERROR(s))goto finish;s=Start(c,p,5,sibling);if(EFI_ERROR(s))goto finish;
  s=Start(c,p,1,physical);if(EFI_ERROR(s))goto finish;
  EFI_HANDLE bridge=NULL,text=NULL;s=GcLeafPair(c,physical,&GopGuid,&bridge,&text);
  if(EFI_ERROR(s) || bridge!=p->drivers[1].driver.handle){s=EFI_COMPROMISED_DATA;goto finish;}
  s=Start(c,p,2,text);if(EFI_ERROR(s))goto finish;s=Start(c,p,3,text);r->connect=s;if(EFI_ERROR(s))goto finish;
  s=Restored(c,p,fresh,sibling);r->verify=s;r->restored=!EFI_ERROR(s);
finish:
  c->log("FULL_RESULT status=%#llx touched=%u detach=%#llx isolated=%#llx exclusive=%#llx close=%#llx restore=%#llx verify=%#llx restored=%u\n",
    (unsigned long long)s,r->touched,(unsigned long long)r->detach,(unsigned long long)r->isolated,(unsigned long long)r->claim,
    (unsigned long long)r->close,(unsigned long long)r->connect,(unsigned long long)r->verify,r->restored);
  return Free(c,fresh,s);
}
