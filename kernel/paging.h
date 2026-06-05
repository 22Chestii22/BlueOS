#ifndef PAGING_H
#define PAGING_H

#include "types.h"

extern uint64_t kernel_cr3;
void paging_init(uint64_t mem_size);
void map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void map_page_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
void unmap_page(uint64_t virt);
uint64_t paging_create_pml4(void);
int paging_map_user(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
void paging_free_pml4(uint64_t pml4_phys);
void paging_switch(uint64_t cr3);
uint32_t paging_get_total_frames(void);
uint32_t paging_get_used_frames(void);
uint64_t paging_alloc_frame(void);

#endif

