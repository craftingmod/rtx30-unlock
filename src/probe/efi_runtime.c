#include <Uefi.h>
/* Freestanding compiler intrinsics only. These operate on ordinary host RAM. */
VOID *memset(VOID *Destination, int Value, UINTN Size) {
  volatile UINT8 *D = Destination;
  UINTN I;
  for (I = 0; I < Size; I++)
    D[I] = (UINT8)Value;
  return Destination;
}
VOID *memcpy(VOID *Destination, const VOID *Source, UINTN Size) {
  volatile UINT8 *D = Destination;
  const volatile UINT8 *S = Source;
  UINTN I;
  for (I = 0; I < Size; I++)
    D[I] = S[I];
  return Destination;
}
