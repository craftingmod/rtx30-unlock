/* Minimal stand-in for the hosted libc <string.h> when building without the
   MSVC/UCRT headers. The implementations live in src/unlock/runtime.c. */
#pragma once
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
void *memchr(const void *source, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);
int strcmp(const char *left, const char *right);
size_t strlen(const char *s);

size_t strnlen(const char *s, size_t max);
int strncmp(const char *left, const char *right, size_t size);
void *memmove(void *destination, const void *source, size_t size);
