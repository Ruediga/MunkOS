#ifndef KPRINTF_H
#define KPRINTF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cpu.h"

#include <stdarg.h>

extern const char *kernel_okay_string;
// DO NOT TOUCH THIS OUTSIDE OF kpanic()
extern k_spinlock_t kprintf_lock;

int kprintf(const char *format, ...);
int kvprintf(const char* format, va_list var_args);

#ifdef __cplusplus
}
#endif

#endif // KPRINTF_H