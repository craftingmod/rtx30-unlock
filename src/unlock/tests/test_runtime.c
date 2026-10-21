#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include "../format.h"
void *GuTestMemcpy(void *, const void *, size_t);
void *GuTestMemset(void *, int, size_t);
void *GuTestMemchr(const void *, int, size_t);
int GuTestMemcmp(const void *, const void *, size_t);
int GuTestStrcmp(const char *, const char *);
size_t GuTestStrlen(const char *);
#ifdef GU_EMPTY_RUNTIME
void *GuTestMemcpy(void *d, const void *s, size_t n) {
  (void)s;
  (void)n;
  return d;
}
void *GuTestMemset(void *d, int v, size_t n) {
  (void)v;
  (void)n;
  return d;
}
void *GuTestMemchr(const void *s, int v, size_t n) {
  (void)s;
  (void)v;
  (void)n;
  return NULL;
}
int GuTestMemcmp(const void *a, const void *b, size_t n) {
  (void)a;
  (void)b;
  (void)n;
  return 0;
}
int GuTestStrcmp(const char *a, const char *b) {
  (void)a;
  (void)b;
  return 0;
}
size_t GuTestStrlen(const char *s) {
  (void)s;
  return 0;
}
size_t GuFormatV(char *d, size_t n, const char *f, va_list a) {
  (void)f;
  (void)a;
  if (n) d[0] = 0;
  return 0;
}
#endif
static size_t fmt(char *d, size_t n, const char *f, ...) {
  va_list a;
  va_start(a, f);
  size_t r = GuFormatV(d, n, f, a);
  va_end(a);
  return r;
}
int main(int argc, char **argv) {
  (void)argv;
  char b[256];
  unsigned char m[] = {0xaa, 0, 0xff, 0x7f, 0xbb};
  if (argc == 1) {
    assert(GuTestMemchr(m, 0xff, 5) == m + 2);
    assert(GuTestMemchr(m, 0x1ff, 5) == m + 2 && !GuTestMemchr(m, 0xff, 2));
    assert(!GuTestMemchr(m, 0xaa, 0));
    assert(GuTestMemcmp(m + 2, m + 3, 1) > 0 && GuTestMemcmp(m + 3, m + 2, 1) < 0);
    assert(!GuTestMemcmp(m, m, 5) && !GuTestMemcmp(m, m + 1, 0));
    assert(GuTestStrcmp("\xff", "\x7f") > 0 && GuTestStrcmp("a", "ab") < 0);
    assert(!GuTestStrcmp("", ""));
    assert(GuTestStrlen("abc") == 3 && !GuTestStrlen(""));
    assert(GuTestMemset(m + 1, 0x166, 2) == m + 1 && m[0] == 0xaa && m[1] == 0x66 && m[2] == 0x66 &&
           m[3] == 0x7f);
    assert(GuTestMemcpy(m + 1, "12", 2) == m + 1 && m[0] == 0xaa && m[1] == '1' && m[2] == '2' &&
           m[3] == 0x7f);
  }
  const char *expect = "0xffffffff 00000008 -2147483648 4294967295 -2147483648 4294967295 "
                       "-9223372036854775808 18446744073709551615 4294967296 ok % A";
  assert(fmt(b, sizeof(b), "%#x %08x %d %u %ld %lu %lld %llu %zu %s %% %c", 0xffffffffU, 8,
             -2147483647 - 1, 0xffffffffU, -2147483647L - 1, 0xffffffffUL, INT64_MIN, UINT64_MAX,
             (size_t)0x100000000ULL, "ok", 'A') == strlen(expect));
  assert(!strcmp(b, expect));
  assert(fmt(b, 5, "%s", "abcdef") == 6 && !strcmp(b, "abcd"));
  assert(fmt(b, 1, "abc") == 3 && !b[0]);
  assert(fmt(NULL, 0, "abcd") == 4);
  assert(fmt(b, sizeof(b), "%#08x %5d %#x", 0x12, -3, 0) == 16 && !strcmp(b, "0x000012    -3 0"));
  assert(fmt(b, sizeof(b), "%s", (char *)NULL) == 6 && !strcmp(b, "(null)"));
  assert(fmt(b, sizeof(b), "%n") == SIZE_MAX && !b[0]);
  assert(fmt(b, sizeof(b), "%9999x", 1) == SIZE_MAX && !b[0]);
  puts("Freestanding runtime and LLP64 bounded formatter: PASS");
  return 0;
}
