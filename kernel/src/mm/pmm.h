#pragma once

#include <limine.h>

#define PAGE_SIZE 4096

void initPMM(void);
void *claimContinousPages(uint64_t count);