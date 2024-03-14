#pragma once

#include <stddef.h>
#include <stdint.h>

// symbol table created is of following structure:
struct stacktrace_symbol_table_entry {
    uintptr_t address;
    char *symbol_name;
};

extern struct stacktrace_symbol_table_entry stacktrace_symtable[];

void stacktrace();
void stacktrace_at(uintptr_t rbp);