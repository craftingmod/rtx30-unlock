/* Minimal declaration-only <string.h> for building the EFI application without a
 * Windows SDK / MSVC CRT. The implementations live in the repository
 * (src/unlock/runtime.c / src/unlock/compat.c). Used via CPATH, see
 * docs/build-without-msvc.md. */
#pragma once
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
