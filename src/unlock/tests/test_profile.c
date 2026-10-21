#include "../checks.h"
#include "../vendor/layout_610.h"
#include "../../probe/gpu_family.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

VOID GuLog(const char *Format, ...) { (void)Format; }

static EFI_PCI_IO_PROTOCOL target, display;
static EFI_HANDLE resolved;
static int same_bdf, read_error, wrong_class;
static EFI_STATUS EFIAPI locate(EFI_GUID *Guid, EFI_DEVICE_PATH_PROTOCOL **Path,
                                EFI_HANDLE *Handle) {
  (void)Guid;
  (void)Path;
  *Handle = resolved;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocol(EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Out) {
  (void)Handle;
  (void)Guid;
  *Out = &display;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI location(EFI_PCI_IO_PROTOCOL *Pci, UINTN *S, UINTN *B, UINTN *D,
                                  UINTN *F) {
  *S = 0;
  *B = (Pci == &target || same_bdf) ? 9 : 0;
  *D = 0;
  *F = 0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_config(EFI_PCI_IO_PROTOCOL *Pci, EFI_PCI_IO_PROTOCOL_WIDTH Width,
                                     UINT32 Offset, UINTN Count, VOID *Out) {
  (void)Pci;
  assert(Width == EfiPciIoWidthUint32 && !Offset && Count == 3);
  UINT32 *C = Out;
  C[0] = 0x12348086;
  C[1] = 0;
  C[2] = wrong_class ? 0x06000000 : 0x03000000;
  return read_error ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

int main(void) {
  UINT32 config[64] = {0};
  GU_CAPS caps;
  assert(GU_PROFILE_BUILD);
  config[0] = GU_BOARD_ID;
  config[11] = GU_BOARD_SUBSYSTEM;
  config[1] = 0x00100002;
  config[2] = GU_CLASS_REVISION;
  config[0x34 / 4] = 0x40;
  config[0x40 / 4] = 0x00034801;
  config[0x48 / 4] = 0x00020010;
  config[0x4c / 4] = 0x10000000;
  assert(GuCheckConfig(config, 3, 19, 5, 1, &caps));
  assert(GuTargetConfig(config, 3, 19, 5, 1));
  assert(!GuCheckConfig(config, 0, 256, 0, 0, &caps));
  config[0] ^= 1;
  assert(!GuCheckConfig(config, 3, 19, 5, 1, &caps));
  config[0] ^= 1;
  config[11] ^= 1;
  assert(!GuCheckConfig(config, 3, 19, 5, 1, &caps));
  assert(!GuTargetConfig(config, 3, 19, 5, 1));
  config[11] ^= 1;
  config[1] |= 4;
  assert(!GuCheckConfig(config, 3, 19, 5, 1, &caps));
  config[1] &= ~4U;
  config[0x4c / 4] = 0;
  assert(!GuCheckConfig(config, 3, 19, 5, 1, &caps));
  assert(GpuGa10xPciCandidate(GU_BOARD_ID) && GpuGa10xChip(GU_BOARD_CHIP));
  assert(GU_FB_MIB >= 256U);
  struct gpu_unlock_layout_in in = {0};
  struct gpu_unlock_layout_out out = {0};
  in.usable_fb = (UINT64)GU_FB_MIB << 20;
  in.vga_base = in.usable_fb - (1ULL << 20);
  in.frts_size = 1ULL << 20;
  in.boot_size = 24576;
  in.image_size = 80ULL << 20;
  in.nonwpr_size = 1ULL << 20;
  in.heap_request = 88ULL << 20;
  assert(gpu_unlock_layout(&in, &out) == 0);
  assert(out.wpr_end <= in.usable_fb && out.nonwpr_start >= in.usable_fb - (256ULL << 20));
  assert(out.wpr_start > in.usable_fb - (256ULL << 20));
  EFI_BOOT_SERVICES bs = {0};
  EFI_DEVICE_PATH_PROTOCOL path = {0};
  bs.LocateDevicePath = locate;
  bs.HandleProtocol = protocol;
  target.GetLocation = location;
  display.GetLocation = location;
  display.Pci.Read = read_config;
  resolved = (EFI_HANDLE)2;
  assert(GuIndependentDisplay(&bs, &target, (EFI_HANDLE)1, &path));
  resolved = (EFI_HANDLE)1;
  assert(!GuIndependentDisplay(&bs, &target, (EFI_HANDLE)1, &path));
  resolved = (EFI_HANDLE)2;
  same_bdf = 1;
  assert(!GuIndependentDisplay(&bs, &target, (EFI_HANDLE)1, &path));
  same_bdf = 0;
  wrong_class = 1;
  assert(!GuIndependentDisplay(&bs, &target, (EFI_HANDLE)1, &path));
  wrong_class = 0;
  read_error = 1;
  assert(!GuIndependentDisplay(&bs, &target, (EFI_HANDLE)1, &path));
  read_error = 0;
  bs.LocateDevicePath = NULL;
  assert(!GuIndependentDisplay(&bs, &target, (EFI_HANDLE)1, &path));
  printf("Profile chip=%#x FB=%u MiB: target/BDF, layout, independent display and refusal checks "
         "PASS\n",
         GU_BOARD_CHIP, GU_FB_MIB);
  return 0;
}
