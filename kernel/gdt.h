#ifndef GDT_H
#define GDT_H

#include "types.h"

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t rsp0);

#endif
