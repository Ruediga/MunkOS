#pragma once

#include <stddef.h>
#include <stdint.h>

// symbol table created is of following structure:
struct stacktrace_symbol_table_entry {
    uintptr_t address;
    const char *symbol_name;
};

struct stacktrace_rbp_rel_stack_frame {
    struct stacktrace_rbp_rel_stack_frame *rbp;
    uintptr_t rsp;
};

extern struct stacktrace_symbol_table_entry stacktrace_symtable[];

void stacktrace();
void stacktrace_at(uintptr_t rbp);