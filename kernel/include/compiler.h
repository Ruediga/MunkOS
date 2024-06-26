#pragma once

// gcc macros

#define unreachable() __builtin_unreachable()

#define comp_packed __attribute__((packed))
#define comp_aligned(n) __attribute__((aligned(n)))
#define comp_noreturn __attribute__((noreturn))
#define comp_weak __attribute__((weak))

#define comp_no_asan __attribute__((no_sanitize_address))
#define comp_no_ubsan __attribute__((no_sanitize("undefined")))