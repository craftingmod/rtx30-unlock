#include "cycle.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextOut.h>
#include <string.h>

static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID, PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                BindingGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID,
                LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID,
                TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;

static BOOLEAN EqualGuid(const EFI_GUID *a, const EFI_GUID *b) { return !memcmp(a, b, sizeof(*a)); }
static EFI_STATUS Release(GC_CONTEXT *c, VOID *p, EFI_STATUS status) {
  if (p && EFI_ERROR(c->table->BootServices->FreePool(p))) return EFI_DEVICE_ERROR;
  return status;
}
static EFI_STATUS Driver(GC_CONTEXT *c, GC_SNAPSHOT *s, EFI_HANDLE agent) {
  for (UINTN i = 0; i < s->driver_count; i++)
    if (s->drivers[i].handle == agent) return EFI_SUCCESS;
  if (!agent || agent == c->image || agent == c->target || s->driver_count == GC_LIMIT)
    return EFI_UNSUPPORTED;
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
  EFI_LOADED_IMAGE_PROTOCOL *image = NULL;
  EFI_STATUS status = bs->HandleProtocol(agent, &BindingGuid, (VOID **)&binding);
  if (EFI_ERROR(status) || !binding || binding->DriverBindingHandle != agent || !binding->Start ||
      !binding->Stop || !binding->ImageHandle || binding->ImageHandle == c->image)
    return EFI_UNSUPPORTED;
  status = bs->HandleProtocol(binding->ImageHandle, &LoadedGuid, (VOID **)&image);
  if (EFI_ERROR(status) || !image || !image->ImageBase || !image->ImageSize ||
      image->ImageSize > 64U * 1024U * 1024U || image->ImageCodeType != EfiBootServicesCode)
    return EFI_UNSUPPORTED;
  GC_DRIVER *d = &s->drivers[s->driver_count++];
  d->handle = agent;
  d->image = binding->ImageHandle;
  d->base = image->ImageBase;
  d->bytes = image->ImageSize;
  d->start = binding->Start;
  d->stop = binding->Stop;
  d->version = binding->Version;
  return c->log("driver=%#llx image=%#llx base=%#llx bytes=%llu version=%#x\n",
                (unsigned long long)(UINTN)agent, (unsigned long long)(UINTN)d->image,
                (unsigned long long)(UINTN)d->base, (unsigned long long)d->bytes, d->version)
             ? EFI_SUCCESS
             : EFI_DEVICE_ERROR;
}
static EFI_STATUS Owners(GC_CONTEXT *c, GC_SNAPSHOT *s, EFI_HANDLE handle, BOOLEAN root) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_GUID **protocols = NULL;
  UINTN count = 0;
  EFI_STATUS status = bs->ProtocolsPerHandle(handle, &protocols, &count);
  if (!EFI_ERROR(status) && (!protocols || !count || count > 64)) status = EFI_UNSUPPORTED;
  for (UINTN i = 0; !EFI_ERROR(status) && i < count; i++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries = NULL;
    UINTN n = 0;
    if (!protocols[i]) {
      status = EFI_COMPROMISED_DATA;
      break;
    }
    status = bs->OpenProtocolInformation(handle, protocols[i], &entries, &n);
    if (!EFI_ERROR(status) && (n > 128 || (n && !entries))) status = EFI_UNSUPPORTED;
    for (UINTN j = 0; !EFI_ERROR(status) && j < n; j++) {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e = &entries[j];
      if (!c->log(
              "open handle=%#llx protocol=%08x attr=%#x count=%u agent=%#llx controller=%#llx\n",
              (unsigned long long)(UINTN)handle, protocols[i]->Data1, e->Attributes, e->OpenCount,
              (unsigned long long)(UINTN)e->AgentHandle,
              (unsigned long long)(UINTN)e->ControllerHandle)) {
        status = EFI_DEVICE_ERROR;
        break;
      }
      if (e->Attributes == EFI_OPEN_PROTOCOL_GET_PROTOCOL ||
          e->Attributes == EFI_OPEN_PROTOCOL_TEST_PROTOCOL ||
          e->Attributes == EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL)
        continue;
      if (e->OpenCount != 1) {
        status = EFI_ACCESS_DENIED;
        break;
      }
      status = Driver(c, s, e->AgentHandle);
      if (EFI_ERROR(status)) break;
      if (e->Attributes == EFI_OPEN_PROTOCOL_BY_DRIVER && e->ControllerHandle == handle) {
        if (root) {
          if (s->owner && s->owner != e->AgentHandle) {
            status = EFI_ACCESS_DENIED;
            break;
          }
          s->owner = e->AgentHandle;
        }
      } else if (root &&
                 (EqualGuid(protocols[i], &PciGuid) || EqualGuid(protocols[i], &PathGuid)) &&
                 e->Attributes == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e->ControllerHandle &&
                 e->ControllerHandle != handle) {
        UINTN k;
        for (k = 0; k < s->child_count; k++)
          if (s->children[k] == e->ControllerHandle) break;
        if (k < s->child_count) {
          if (s->child_agents[k] != e->AgentHandle) status = EFI_ACCESS_DENIED;
        } else if (k == GC_LIMIT)
          status = EFI_UNSUPPORTED;
        else {
          s->children[k] = e->ControllerHandle;
          s->child_agents[k] = e->AgentHandle;
          s->child_count++;
        }
      } else
        status = EFI_ACCESS_DENIED;
    }
    status = Release(c, entries, status);
  }
  return Release(c, protocols, status);
}
static EFI_STATUS Refresh(GC_CONTEXT *c) {
  EFI_PCI_IO_PROTOCOL *pci = NULL;
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_STATUS status = bs->HandleProtocol(c->target, &PciGuid, (VOID **)&pci);
  if (EFI_ERROR(status) || !pci || !pci->GetLocation || !pci->Pci.Read) return EFI_NOT_FOUND;
  UINT32 id = 0, subsystem = 0, class_revision = 0;
  UINTN location[4];
  status = pci->GetLocation(pci, &location[0], &location[1], &location[2], &location[3]);
  if (EFI_ERROR(status) || memcmp(location, c->location, sizeof(location))) return EFI_UNSUPPORTED;
  status = pci->Pci.Read(pci, EfiPciIoWidthUint32, 0, 1, &id);
  if (EFI_ERROR(status)) return status;
  status = pci->Pci.Read(pci, EfiPciIoWidthUint32, 0x2c, 1, &subsystem);
  if (EFI_ERROR(status)) return status;
  status = pci->Pci.Read(pci, EfiPciIoWidthUint32, 8, 1, &class_revision);
  if (EFI_ERROR(status)) return status;
  /* Match the device selected this launch, without a board/model allowlist. */
  if ((id & 0xffffU) != 0x10de || (class_revision >> 24) != 3 || id != c->identity ||
      subsystem != c->subsystem || class_revision != c->class_revision)
    return EFI_UNSUPPORTED;
  c->pci = pci;
  return EFI_SUCCESS;
}
EFI_STATUS GcCapture(GC_CONTEXT *c, GC_SNAPSHOT *s) {
  memset(s, 0, sizeof(*s));
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_STATUS status = Refresh(c);
  if (EFI_ERROR(status)) return status;
  status = Owners(c, s, c->target, TRUE);
  if (EFI_ERROR(status)) {
    c->log("capture root-owners status=%#llx\n", (unsigned long long)status);
    return status;
  }
  /* Device Path may enumerate before PCI I/O. Validate every collected edge after finding the owner. */
  for (UINTN i = 0; i < s->child_count; i++)
    if (!s->owner || s->child_agents[i] != s->owner) {
      c->log("capture child-owner mismatch child=%#llx agent=%#llx owner=%#llx\n",
             (unsigned long long)(UINTN)s->children[i],
             (unsigned long long)(UINTN)s->child_agents[i], (unsigned long long)(UINTN)s->owner);
      return EFI_ACCESS_DENIED;
    }
  /* A single direct GOP subtree only; deeper/foreign relationships need a new experiment. */
  for (UINTN i = 0; i < s->child_count; i++) {
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    EFI_HANDLE resolved = NULL;
    status = bs->HandleProtocol(s->children[i], &PathGuid, (VOID **)&path);
    if (!EFI_ERROR(status))
      status = path ? bs->LocateDevicePath(&PciGuid, &path, &resolved) : EFI_COMPROMISED_DATA;
    if (!c->log(
            "capture child-path child=%#llx agent=%#llx target=%#llx resolved=%#llx status=%#llx\n",
            (unsigned long long)(UINTN)s->children[i],
            (unsigned long long)(UINTN)s->child_agents[i], (unsigned long long)(UINTN)c->target,
            (unsigned long long)(UINTN)resolved, (unsigned long long)status))
      return EFI_DEVICE_ERROR;
    if (EFI_ERROR(status)) return status;
    if (resolved != c->target) return EFI_ACCESS_DENIED;
    status = Owners(c, s, s->children[i], FALSE);
    if (EFI_ERROR(status)) {
      c->log("capture child-owners status=%#llx\n", (unsigned long long)status);
      return status;
    }
  }
  EFI_HANDLE *gops = NULL;
  UINTN count = 0;
  status = bs->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &count, &gops);
  if (status == EFI_NOT_FOUND) return EFI_SUCCESS;
  if (!EFI_ERROR(status) && (!gops || !count || count > GC_LIMIT)) status = EFI_UNSUPPORTED;
  for (UINTN i = 0; !EFI_ERROR(status) && i < count; i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    status = bs->HandleProtocol(gops[i], &GopGuid, (VOID **)&gop);
    if (EFI_ERROR(status) || !gop || !gop->Mode) {
      status = EFI_UNSUPPORTED;
      break;
    }
    EFI_STATUS found = bs->HandleProtocol(gops[i], &PathGuid, (VOID **)&path);
    if ((found == EFI_UNSUPPORTED || found == EFI_NOT_FOUND) && !path &&
        !gop->Mode->FrameBufferBase && !gop->Mode->FrameBufferSize)
      continue;
    EFI_HANDLE resolved = NULL;
    if (EFI_ERROR(found) || !path || EFI_ERROR(bs->LocateDevicePath(&PciGuid, &path, &resolved)) ||
        resolved != c->target) {
      status = EFI_UNSUPPORTED;
      break;
    }
    BOOLEAN known = gops[i] == c->target;
    for (UINTN j = 0; j < s->child_count; j++)
      if (gops[i] == s->children[j]) known = TRUE;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m = gop->Mode;
    if (!known || !m->Info || m->SizeOfInfo < sizeof(*m->Info) || !m->FrameBufferBase ||
        !m->FrameBufferSize || m->Mode >= m->MaxMode ||
        m->Info->PixelFormat > PixelBlueGreenRedReserved8BitPerColor ||
        !m->Info->HorizontalResolution || !m->Info->VerticalResolution ||
        m->Info->PixelsPerScanLine < m->Info->HorizontalResolution ||
        (UINT64)m->Info->PixelsPerScanLine * m->Info->VerticalResolution > m->FrameBufferSize / 4) {
      status = EFI_UNSUPPORTED;
      break;
    }
    s->mode = *m->Info;
    s->physical_gops++;
    if (!c->log("physical_gop=%#llx framebuffer=%#llx bytes=%zu size=%ux%u\n",
                (unsigned long long)(UINTN)gops[i], (unsigned long long)m->FrameBufferBase,
                (size_t)m->FrameBufferSize, m->Info->HorizontalResolution,
                m->Info->VerticalResolution)) {
      status = EFI_DEVICE_ERROR;
      break;
    }
    for (UINTN j = 0; j <= s->child_count; j++) {
      EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text = NULL;
      EFI_HANDLE h = j ? s->children[j - 1] : c->target;
      EFI_STATUS t = bs->HandleProtocol(h, &TextGuid, (VOID **)&text);
      if (!EFI_ERROR(t) && text && text->OutputString) s->text_outputs++;
    }
  }
  return Release(c, gops, status);
}
BOOLEAN GcReady(const GC_SNAPSHOT *s) {
  return s->owner && s->driver_count && s->physical_gops == 1 && s->text_outputs;
}
BOOLEAN GcSame(const GC_SNAPSHOT *a, const GC_SNAPSHOT *b) {
  if (a->owner != b->owner || a->child_count != b->child_count ||
      a->driver_count != b->driver_count || a->physical_gops != b->physical_gops ||
      a->text_outputs != b->text_outputs || memcmp(&a->mode, &b->mode, sizeof(a->mode)))
    return FALSE;
  for (UINTN i = 0; i < a->driver_count; i++) {
    BOOLEAN found = FALSE;
    for (UINTN j = 0; j < b->driver_count; j++) {
      const GC_DRIVER *x = &a->drivers[i], *y = &b->drivers[j];
      if (x->handle == y->handle && x->image == y->image && x->base == y->base &&
          x->bytes == y->bytes && x->start == y->start && x->stop == y->stop &&
          x->version == y->version)
        found = TRUE;
    }
    if (!found) return FALSE;
  }
  return TRUE;
}
EFI_STATUS GcCycle(GC_CONTEXT *c, const GC_SNAPSHOT *approved, GC_RESULT *r) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  GC_SNAPSHOT fresh;
  memset(r, 0, sizeof(*r));
  r->detach = r->isolated = r->claim = r->close = r->connect = r->verify = EFI_NOT_STARTED;
  EFI_STATUS status = GcCapture(c, &fresh);
  if (EFI_ERROR(status)) return status;
  if (!GcReady(&fresh) || !GcSame(approved, &fresh)) return EFI_ABORTED;
  for (UINTN i = 0; i < approved->child_count; i++) {
    BOOLEAN found = FALSE;
    for (UINTN j = 0; j < fresh.child_count; j++)
      if (approved->children[i] == fresh.children[j]) found = TRUE;
    if (!found) return EFI_ABORTED;
  }
  /* Durable intent precedes the only disconnect. No console or input calls below. */
  if (!c->log("DETACH_INTENT: one target driver; no unlock, FLR or direct GPU writes\n"))
    return EFI_DEVICE_ERROR;
  r->touched = TRUE;
  r->detach = bs->DisconnectController(c->target, approved->owner, NULL);
  r->isolated = GcCapture(c, &fresh);
  if (!EFI_ERROR(r->isolated) &&
      (fresh.owner || fresh.child_count || fresh.driver_count || fresh.physical_gops))
    r->isolated = EFI_ACCESS_DENIED;
  if (!EFI_ERROR(r->detach) && !EFI_ERROR(r->isolated)) {
    EFI_PCI_IO_PROTOCOL *pci = NULL;
    r->claim = bs->OpenProtocol(c->target, &PciGuid, (VOID **)&pci, c->image, NULL,
                                EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if (!EFI_ERROR(r->claim)) {
      /* End exclusive ownership before any reconnect, even on a changed interface. */
      if (pci != c->pci) r->isolated = EFI_COMPROMISED_DATA;
      r->close = bs->CloseProtocol(c->target, &PciGuid, c->image, NULL);
      if (EFI_ERROR(r->close)) return r->close;
    }
  }
  /* Disconnect can fail after partial effects. Recovery is still mandatory. */
  EFI_HANDLE drivers[GC_LIMIT + 1] = {0};
  UINTN used = 0;
  drivers[used++] = approved->owner;
  for (UINTN i = 0; i < approved->driver_count; i++)
    if (approved->drivers[i].handle != approved->owner)
      drivers[used++] = approved->drivers[i].handle;
  r->connect = bs->ConnectController(c->target, drivers, NULL, TRUE);
  r->verify = GcCapture(c, &fresh);
  r->restored = !EFI_ERROR(r->connect) && !EFI_ERROR(r->verify) && GcReady(&fresh) &&
                GcSame(approved, &fresh);
  if (!r->restored) return EFI_DEVICE_ERROR;
  if (!c->log("RESTORED: GOP, text-output protocol and original driver set observed; visual check "
              "required\n"))
    return EFI_DEVICE_ERROR;
  return !EFI_ERROR(r->detach) && !EFI_ERROR(r->isolated) && !EFI_ERROR(r->claim) &&
                 !EFI_ERROR(r->close)
             ? EFI_SUCCESS
             : EFI_DEVICE_ERROR;
}
