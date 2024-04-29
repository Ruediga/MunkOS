#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

#define UACPI_PRIx64 PRIx64
#define UACPI_PRIX64 PRIX64
#define UACPI_PRIu64 PRIu64

#define uacpi_offsetof(t, m) ((uintptr_t)(&((t*)0)->m))

void  *uacpi_memcpy(void *dest, const void* src, size_t sz);
void  *uacpi_memset(void *dest, int src, size_t cnt);
int    uacpi_memcmp(const void *src1, const void *src2, size_t cnt);
int    uacpi_strncmp(const char *src1, const char *src2, size_t maxcnt);
int    uacpi_strcmp(const char *src1, const char *src2);
void  *uacpi_memmove(void *dest, const void* src, size_t sz);
size_t uacpi_strnlen(const char *src, size_t maxcnt);
size_t uacpi_strlen(const char *src);
int    uacpi_snprintf(char* dest, size_t n, const char* format, ...);