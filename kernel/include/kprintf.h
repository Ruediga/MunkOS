#ifndef KPRINTF_H
#define KPRINTF_H

#ifdef __cplusplus
extern "C" {
#endif

extern const char *kernel_okay_string;

int kprintf(const char *, ...);

#ifdef __cplusplus
}
#endif

#endif // KPRINTF_H