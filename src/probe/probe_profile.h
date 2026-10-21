#pragma once
#include "probe_core.h"
#include <Protocol/SimpleFileSystem.h>
#define PROBE_PROFILE_BYTES 176U
BOOLEAN ProbeEncodeProfile(const PROBE_RESULT *result, const UINT8 prom_hash[32],
                           const UINT8 report_hash[32], UINT8 output[PROBE_PROFILE_BYTES]);
EFI_STATUS ProbeWriteFile(EFI_FILE_PROTOCOL *file, VOID *data, UINTN size);
EFI_STATUS ProbeSaveNew(EFI_FILE_PROTOCOL *directory, CHAR16 *name, VOID *data, UINTN size);
EFI_STATUS ProbeCaptureProfile(EFI_SYSTEM_TABLE *table, EFI_FILE_PROTOCOL *output,
                               UINT8 bytes[PROBE_PROFILE_BYTES]);
EFI_STATUS GuProfileCreate(EFI_SYSTEM_TABLE *table, EFI_FILE_PROTOCOL *parent, BOOLEAN debug);
EFI_STATUS GuProfilePromChanged(EFI_SYSTEM_TABLE *table, EFI_PCI_IO_PROTOCOL *pci,
                                BOOLEAN *changed);
EFI_STATUS GuProfileRefresh(EFI_SYSTEM_TABLE *table, EFI_FILE_PROTOCOL *parent, BOOLEAN debug);
EFI_STATUS GuProfileCommit(EFI_FILE_PROTOCOL *parent);
