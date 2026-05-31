#ifndef MEM_H
#define MEM_H

#include "types.h"

void mem_init(void);
void* malloc(uint32_t size);
void free(void* ptr);
void* calloc(uint32_t num, uint32_t size);
void* realloc(void* ptr, uint32_t new_size);
uint32_t mem_get_used(void);
uint32_t mem_get_free(void);

#endif
