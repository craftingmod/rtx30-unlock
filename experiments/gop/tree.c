#include "tree.h"
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextOut.h>
#include <string.h>

#define TREE_NODES 64
#define TREE_DEPTH 8
#define TREE_OPENS 4096
static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID, PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,
                BindingGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID,
                LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
typedef struct {
  EFI_HANDLE handle;
  UINTN depth;
} TREE_NODE;

static EFI_STATUS Free(GC_CONTEXT *c, VOID *p, EFI_STATUS status) {
  return p && EFI_ERROR(c->table->BootServices->FreePool(p)) ? EFI_DEVICE_ERROR : status;
}
static const char *Role(const EFI_GUID *g) {
  if (!memcmp(g, &PciGuid, sizeof(*g))) return "PCI_IO";
  if (!memcmp(g, &PathGuid, sizeof(*g))) return "DEVICE_PATH";
  if (!memcmp(g, &GopGuid, sizeof(*g))) return "GOP";
  if (!memcmp(g, &TextGuid, sizeof(*g))) return "TEXT_OUT";
  return "OTHER";
}
static BOOLEAN Driver(GC_CONTEXT *c, EFI_HANDLE agent) {
  EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
  EFI_LOADED_IMAGE_PROTOCOL *image = NULL;
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_STATUS status = bs->HandleProtocol(agent, &BindingGuid, (VOID **)&binding);
  if (!c->log("TREE_DRIVER agent=%#llx binding_status=%#llx\n", (unsigned long long)(UINTN)agent,
              (unsigned long long)status))
    return FALSE;
  if (EFI_ERROR(status) || !binding) return TRUE;
  if (!c->log(
          "TREE_BINDING agent=%#llx binding=%#llx image=%#llx version=%#x start=%#llx stop=%#llx\n",
          (unsigned long long)(UINTN)agent, (unsigned long long)(UINTN)binding->DriverBindingHandle,
          (unsigned long long)(UINTN)binding->ImageHandle, binding->Version,
          (unsigned long long)(UINTN)binding->Start, (unsigned long long)(UINTN)binding->Stop))
    return FALSE;
  if (!binding->ImageHandle) return TRUE;
  status = bs->HandleProtocol(binding->ImageHandle, &LoadedGuid, (VOID **)&image);
  if (!c->log("TREE_IMAGE agent=%#llx status=%#llx\n", (unsigned long long)(UINTN)agent,
              (unsigned long long)status))
    return FALSE;
  if (EFI_ERROR(status) || !image) return TRUE;
  return c->log("TREE_IMAGE_INFO agent=%#llx base=%#llx bytes=%llu code_type=%u source=%#llx\n",
                (unsigned long long)(UINTN)agent, (unsigned long long)(UINTN)image->ImageBase,
                (unsigned long long)image->ImageSize, (unsigned)image->ImageCodeType,
                (unsigned long long)(UINTN)image->DeviceHandle);
}
EFI_STATUS GcReadTree(GC_CONTEXT *c) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  TREE_NODE nodes[TREE_NODES] = {{c->target, 0}};
  EFI_HANDLE drivers[TREE_NODES];
  UINTN queued = 1, visited = 0, driver_count = 0, opens = 0;
  EFI_STATUS overall = EFI_SUCCESS;
  if (!c->log("TREE_BEGIN root=%#llx max_nodes=%u max_depth=%u max_opens=%u\n",
              (unsigned long long)(UINTN)c->target, TREE_NODES, TREE_DEPTH, TREE_OPENS))
    return EFI_DEVICE_ERROR;
  while (visited < queued) {
    TREE_NODE node = nodes[visited++];
    EFI_GUID **protocols = NULL;
    UINTN count = 0;
    if (!c->log("TREE_NODE handle=%#llx depth=%zu console_out=%u console_in=%u stderr=%u\n",
                (unsigned long long)(UINTN)node.handle, (size_t)node.depth,
                node.handle == c->table->ConsoleOutHandle, node.handle == c->table->ConsoleInHandle,
                node.handle == c->table->StandardErrorHandle))
      return EFI_DEVICE_ERROR;
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    EFI_HANDLE resolved = NULL;
    EFI_STATUS path_status = bs->HandleProtocol(node.handle, &PathGuid, (VOID **)&path),
               resolved_status = EFI_NOT_FOUND;
    if (!EFI_ERROR(path_status) && path)
      resolved_status = bs->LocateDevicePath(&PciGuid, &path, &resolved);
    if (!c->log("TREE_PATH handle=%#llx path_status=%#llx resolve_status=%#llx pci=%#llx "
                "matches_target=%u\n",
                (unsigned long long)(UINTN)node.handle, (unsigned long long)path_status,
                (unsigned long long)resolved_status, (unsigned long long)(UINTN)resolved,
                resolved == c->target))
      return EFI_DEVICE_ERROR;
    EFI_STATUS status = bs->ProtocolsPerHandle(node.handle, &protocols, &count);
    if (!c->log("TREE_PROTOCOLS handle=%#llx status=%#llx count=%zu\n",
                (unsigned long long)(UINTN)node.handle, (unsigned long long)status, (size_t)count))
      return Free(c, protocols, EFI_DEVICE_ERROR);
    if (!EFI_ERROR(status) && (!protocols || !count || count > 64)) status = EFI_COMPROMISED_DATA;
    for (UINTN i = 0; !EFI_ERROR(status) && i < count; i++) {
      EFI_GUID *g = protocols[i];
      if (!g) {
        status = EFI_COMPROMISED_DATA;
        break;
      }
      if (!c->log("TREE_PROTOCOL handle=%#llx role=%s "
                  "guid=%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
                  (unsigned long long)(UINTN)node.handle, Role(g), g->Data1, (unsigned)g->Data2,
                  (unsigned)g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
                  g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7])) {
        status = EFI_DEVICE_ERROR;
        break;
      }
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries = NULL;
      UINTN n = 0;
      status = bs->OpenProtocolInformation(node.handle, g, &entries, &n);
      if (!c->log("TREE_OPEN_STATUS handle=%#llx protocol=%08x status=%#llx count=%zu\n",
                  (unsigned long long)(UINTN)node.handle, g->Data1, (unsigned long long)status,
                  (size_t)n))
        status = EFI_DEVICE_ERROR;
      if (!EFI_ERROR(status) && (n > 128 || (n && !entries))) status = EFI_COMPROMISED_DATA;
      for (UINTN j = 0; !EFI_ERROR(status) && j < n; j++) {
        if (opens == TREE_OPENS) {
          status = EFI_BUFFER_TOO_SMALL;
          break;
        }
        opens++;
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e = &entries[j];
        if (!c->log("TREE_OPEN handle=%#llx protocol=%08x attr=%#x count=%u agent=%#llx "
                    "controller=%#llx\n",
                    (unsigned long long)(UINTN)node.handle, g->Data1, e->Attributes, e->OpenCount,
                    (unsigned long long)(UINTN)e->AgentHandle,
                    (unsigned long long)(UINTN)e->ControllerHandle)) {
          status = EFI_DEVICE_ERROR;
          break;
        }
        if ((e->Attributes &
             (EFI_OPEN_PROTOCOL_BY_DRIVER | EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER)) &&
            e->AgentHandle) {
          UINTN k;
          for (k = 0; k < driver_count; k++)
            if (drivers[k] == e->AgentHandle) break;
          if (k == driver_count) {
            if (k == TREE_NODES) {
              status = EFI_BUFFER_TOO_SMALL;
              break;
            }
            drivers[driver_count++] = e->AgentHandle;
            if (!Driver(c, e->AgentHandle)) {
              status = EFI_DEVICE_ERROR;
              break;
            }
          }
        }
        if (e->Attributes == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
          if (!e->ControllerHandle) {
            status = EFI_COMPROMISED_DATA;
            break;
          }
          UINTN k;
          for (k = 0; k < queued; k++)
            if (nodes[k].handle == e->ControllerHandle) break;
          BOOLEAN seen = k < queued;
          if (!c->log("TREE_EDGE parent=%#llx child=%#llx agent=%#llx protocol=%08x seen=%u\n",
                      (unsigned long long)(UINTN)node.handle,
                      (unsigned long long)(UINTN)e->ControllerHandle,
                      (unsigned long long)(UINTN)e->AgentHandle, g->Data1, seen)) {
            status = EFI_DEVICE_ERROR;
            break;
          }
          if (!seen) {
            if (queued == TREE_NODES || node.depth == TREE_DEPTH) {
              status = EFI_BUFFER_TOO_SMALL;
              break;
            }
            nodes[queued++] = (TREE_NODE){e->ControllerHandle, node.depth + 1};
          }
        }
      }
      status = Free(c, entries, status);
    }
    status = Free(c, protocols, status);
    if (EFI_ERROR(status)) {
      if (!c->log("TREE_NODE_ERROR handle=%#llx status=%#llx\n",
                  (unsigned long long)(UINTN)node.handle, (unsigned long long)status))
        return EFI_DEVICE_ERROR;
      if (!EFI_ERROR(overall)) overall = status;
      if (status == EFI_BUFFER_TOO_SMALL) break;
    }
  }
  if (!c->log("TREE_END status=%#llx complete=%u visited=%zu queued=%zu drivers=%zu opens=%zu "
              "detach_attempted=0\n",
              (unsigned long long)overall, !EFI_ERROR(overall), (size_t)visited, (size_t)queued,
              (size_t)driver_count, (size_t)opens))
    return EFI_DEVICE_ERROR;
  return overall;
}
