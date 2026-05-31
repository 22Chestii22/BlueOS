#ifndef MODULE_H
#define MODULE_H

#include "types.h"
#include "kernel_api.h"

extern kernel_api_t kernel_api;

char keyb_getchar_wrapper(void);
uint64_t timer_get_ticks_wrapper(void);

void keyb_module_init(kernel_api_t* api);
void timer_module_init(kernel_api_t* api);
void ata_module_init(kernel_api_t* api);
void fat_module_init(kernel_api_t* api);

#endif
