#pragma once
#include "port.h"
VOID GuIoSetQuiet(BOOLEAN quiet);
EFI_STATUS GuIoOpen(EFI_HANDLE Image,EFI_SYSTEM_TABLE *Table);
EFI_STATUS GuIoClose(VOID);
EFI_STATUS GuLoadAssets(VOID);
VOID GuReleaseAssets(VOID);
BOOLEAN GuLogReady(VOID);
VOID GuConsole(const char *Text);
