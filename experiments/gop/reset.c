#include "reset.h"
#include <string.h>
static UINT16 Word(const UINT32 *c, UINTN offset) {
  return (UINT16)(c[offset / 4] >> ((offset & 3) * 8));
}
static EFI_STATUS Read(GC_CONTEXT *c, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT32 o, VOID *v) {
  return c->pci->Pci.Read(c->pci, w, o, 1, v);
}
static EFI_STATUS Write(GC_CONTEXT *c, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT32 o, VOID *v) {
  return c->pci->Pci.Write(c->pci, w, o, 1, v);
}
EFI_STATUS GfSavePci(GC_CONTEXT *c, GF_PCI *p) {
  memset(p, 0, sizeof(*p));
  if (!c->pci || !c->pci->Pci.Read || !c->pci->Pci.Write || !c->pci->Attributes)
    return EFI_UNSUPPORTED;
  EFI_STATUS s = c->pci->Pci.Read(c->pci, EfiPciIoWidthUint32, 0, 64, p->config);
  if (EFI_ERROR(s)) return s;
  if (p->config[0] != c->identity || p->config[11] != c->subsystem ||
      p->config[2] != c->class_revision || !(p->config[1] & 2) || (p->config[1] & 4) ||
      !(p->config[1] & 0x100000) || (p->config[12] & 1))
    return EFI_ACCESS_DENIED;
  UINTN next = p->config[13] & 0xff;
  UINT64 seen = 0;
  BOOLEAN pm = FALSE;
  for (UINTN n = 0; next; n++) {
    if (n == 48 || next < 0x40 || next > 0xfc || (next & 3) || (seen & ((UINT64)1 << (next / 4))))
      return EFI_COMPROMISED_DATA;
    seen |= (UINT64)1 << (next / 4);
    UINT32 h = p->config[next / 4];
    UINT8 id = (UINT8)h;
    if (id == 0x10) {
      UINT8 version = (UINT8)((h >> 16) & 15);
      if (p->pcie || version < 1 || version > 2 || ((h >> 20) & 15) > 1 ||
          next > (version == 2 ? 0xc4 : 0xec) || !(p->config[(next + 4) / 4] & (1U << 28)) ||
          (Word(p->config, next + 8) & 0x8000))
        return EFI_UNSUPPORTED;
      p->pcie = (UINT16)next;
      p->version = version;
      p->control = Word(p->config, next + 8);
      if (version == 2) p->control2 = Word(p->config, next + 0x28);
    } else if (id == 1) {
      if (pm || next > 0xf8 || (Word(p->config, next + 4) & 3)) return EFI_UNSUPPORTED;
      pm = TRUE;
    } else if (id == 5 && (h & 0x10000))
      return EFI_ACCESS_DENIED;
    else if (id == 0x11 && (h & 0x80000000U))
      return EFI_ACCESS_DENIED;
    next = (h >> 8) & 0xff;
  }
  if (!p->pcie || !pm) return EFI_UNSUPPORTED;
  return c->log("FULL_PCI_READY pcie=%#x version=%u command=%#x identity=%#x\n", p->pcie,
                p->version, p->config[1] & 0xffff, p->config[0])
             ? EFI_SUCCESS
             : EFI_DEVICE_ERROR;
}
EFI_STATUS GfResetPci(GC_CONTEXT *c, const GF_PCI *p, BOOLEAN *attempted) {
  *attempted = FALSE;
  UINT32 id = 0, cap = 0;
  UINT16 command = 0xffff, pending = 0xffff, control = 0;
  EFI_STATUS s = Read(c, EfiPciIoWidthUint32, 0, &id);
  if (EFI_ERROR(s) || id != p->config[0]) return EFI_DEVICE_ERROR;
  s = Read(c, EfiPciIoWidthUint16, 4, &command);
  if (EFI_ERROR(s) || command == 0xffff || (command & 4)) return EFI_ACCESS_DENIED;
  s = Read(c, EfiPciIoWidthUint32, p->pcie + 4, &cap);
  if (EFI_ERROR(s) || !(cap & (1U << 28))) return EFI_UNSUPPORTED;
  for (UINTN wait = 0;; wait += 1000) {
    s = Read(c, EfiPciIoWidthUint16, p->pcie + 10, &pending);
    if (EFI_ERROR(s) || pending == 0xffff) return EFI_DEVICE_ERROR;
    if (!(pending & 0x20)) break;
    if (wait == 1000000) return EFI_TIMEOUT;
    c->table->BootServices->Stall(1000);
  }
  s = Read(c, EfiPciIoWidthUint16, p->pcie + 8, &control);
  if (EFI_ERROR(s) || (control & 0x8000)) return EFI_NOT_READY;
  if (!c->log("FULL_FLR_INTENT BME=0 pending=0; one FLR; no device access for 100ms\n"))
    return EFI_DEVICE_ERROR;
  control |= 0x8000;
  *attempted = TRUE;
  s = Write(c, EfiPciIoWidthUint16, p->pcie + 8, &control);
  c->table->BootServices->Stall(100000);
  if (EFI_ERROR(s)) return s;
  for (UINTN wait = 0;; wait += 1000) {
    s = Read(c, EfiPciIoWidthUint32, 0, &id);
    if (!EFI_ERROR(s) && id == p->config[0]) break;
    if (wait == 1000000) return EFI_TIMEOUT;
    c->table->BootServices->Stall(1000);
  }
  s = Read(c, EfiPciIoWidthUint16, 4, &command);
  if (EFI_ERROR(s) || command == 0xffff) return EFI_DEVICE_ERROR;
  if (command & 4) {
    command &= (UINT16)~4U;
    Write(c, EfiPciIoWidthUint16, 4, &command);
    return EFI_ACCESS_DENIED;
  }
  /* Synchronize firmware's decode cache before restoring BARs with decode off. */
  s = c->pci->Attributes(c->pci, EfiPciIoAttributeOperationDisable,
                         EFI_PCI_IO_ATTRIBUTE_IO | EFI_PCI_IO_ATTRIBUTE_MEMORY, NULL);
  if (EFI_ERROR(s)) return s;
  s = Read(c, EfiPciIoWidthUint16, 4, &command);
  if (EFI_ERROR(s) || (command & 7)) return EFI_DEVICE_ERROR;
  for (INTN offset = 0x24; offset >= 0x10; offset -= 4) {
    UINT32 value = p->config[offset / 4];
    s = Write(c, EfiPciIoWidthUint32, (UINT32)offset, &value);
    if (EFI_ERROR(s)) return s;
  }
  UINT32 rom = p->config[12] & ~1U;
  s = Write(c, EfiPciIoWidthUint32, 0x30, &rom);
  if (EFI_ERROR(s)) return s;
  UINT16 cache = (UINT16)p->config[3];
  s = Write(c, EfiPciIoWidthUint16, 0x0c, &cache);
  if (EFI_ERROR(s)) return s;
  UINT8 line = (UINT8)p->config[15];
  s = Write(c, EfiPciIoWidthUint8, 0x3c, &line);
  if (EFI_ERROR(s)) return s;
  UINT16 ctl = p->control;
  s = Write(c, EfiPciIoWidthUint16, p->pcie + 8, &ctl);
  if (EFI_ERROR(s)) return s;
  if (p->version == 2) {
    ctl = p->control2;
    s = Write(c, EfiPciIoWidthUint16, p->pcie + 0x28, &ctl);
    if (EFI_ERROR(s)) return s;
  }
  UINT64 decode = (p->config[1] & 1 ? EFI_PCI_IO_ATTRIBUTE_IO : 0) | EFI_PCI_IO_ATTRIBUTE_MEMORY;
  s = c->pci->Attributes(c->pci, EfiPciIoAttributeOperationEnable, decode, NULL);
  if (EFI_ERROR(s)) return s;
  command = (UINT16)p->config[1] & ~4U;
  s = Write(c, EfiPciIoWidthUint16, 4, &command);
  if (EFI_ERROR(s)) return s;
  UINT16 actual = 0;
  s = Read(c, EfiPciIoWidthUint16, 4, &actual);
  if (EFI_ERROR(s) || actual != command) return EFI_DEVICE_ERROR;
  for (UINTN offset = 0x10; offset <= 0x24; offset += 4) {
    UINT32 actual_bar = 0;
    s = Read(c, EfiPciIoWidthUint32, (UINT32)offset, &actual_bar);
    if (EFI_ERROR(s) || actual_bar != p->config[offset / 4]) return EFI_DEVICE_ERROR;
  }
  c->log("FULL_FLR_PCI_RESTORED command=%#x BARs=matched BME=0\n", actual);
  return EFI_SUCCESS;
}
