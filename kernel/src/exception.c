#include "exception.h"

#include "std/kprintf.h"
void default_exception_handler(void)
{
    printf("Default exception handler called, aborting...\n");
    asm volatile ("cli\n hlt");
}