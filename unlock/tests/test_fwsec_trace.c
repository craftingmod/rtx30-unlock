/* SPDX-License-Identifier: GPL-2.0-only */
/* Execute the same hardware model with current and immutable v0.6 cores. */
#define main transaction_fixture_main
#include "test_efi_transaction.c"
#undef main

int main(int argc, char **argv) {
  static const enum scenario modes[] = {SUCCESS, FWSEC_NO_WPR, FWSEC_TIMEOUT,
                                        PIO_UNAVAILABLE_NO_WPR, FWSEC_ERROR_CODE};
  CHECK(argc == 4);
  load_fixture_assets(argv[1], argv[2]);
  FILE *output = fopen(argv[3], "wb");
  CHECK(output);
  UINT32 header[] = {0x47555431U, (UINT32)ARRAY_SIZE(modes)};
  CHECK(fwrite(header, sizeof(header), 1, output) == 1);
  for (UINTN i = 0; i < ARRAY_SIZE(modes); i++) {
    setup(modes[i]);
    UINT64 mask = 0xffffffffffffULL;
    struct pci_dev device = {
        .dev = {.dma_mask = &mask, .coherent_dma_mask = 0xfffffffffULL}, .pm_cap = 0x60, .refs = 1};
    struct gpu_unlock_610_shared shared = {0};
    CHECK(!gpu_unlock_610_load_shared(&device, &shared));
    int result = gpu_unlock_610_unlock(&device, &shared);
    gpu_unlock_610_release_shared(&shared);
    CHECK(fake.gsp_starts == 1 && !fake.pio_reads && !fake.pio_writes);
    if (modes[i] == SUCCESS) {
      CHECK(!result && GuSafeToReturn() && fake.sec2_starts == 1 && fake.flrs == 1);
      CHECK(!fake.live_maps && !fake.live_buffers);
    } else {
      CHECK(result == (modes[i] == FWSEC_TIMEOUT ? -ETIMEDOUT : -EIO));
      CHECK(!GuSafeToReturn() && Gu.retained && !fake.sec2_starts && !fake.flrs);
      CHECK(fake.live_maps == fake.maps && !fake.unmaps && !fake.frees);
      CHECK(registers[SS0 / 4] == 0x46254040 && registers[SS1 / 4] == 3);
    }
    CHECK(!EFI_ERROR(GuClearMaster()) && !(config[4] & 4));
    UINT32 scenario_header[] = {(UINT32)modes[i], (UINT32)result, (UINT32)fake.trace_count};
    CHECK(fwrite(scenario_header, sizeof(scenario_header), 1, output) == 1);
    CHECK(fwrite(fake.write_trace, sizeof(fake.write_trace[0]), fake.trace_count, output) ==
          fake.trace_count);
    printf("Write trace %s: result=%d writes=%zu\n", case_names[modes[i]], result,
           fake.trace_count);
    dispose_host_fixture();
  }
  CHECK(!fclose(output));
  for (UINTN i = 0; i < 5; i++)
    free((VOID *)GuAssets[i].firmware.data);
  return 0;
}
