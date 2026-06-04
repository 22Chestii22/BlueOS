#ifndef MODULE_H
#define MODULE_H

#include "types.h"
#include "kernel_api.h"
#include "elf_loader.h"
#include "process.h"

extern kernel_api_t kernel_api;

char keyb_getchar_wrapper(void);
int keyb_char_avail_wrapper(void);
uint64_t timer_get_ticks_wrapper(void);


void ata_module_init(kernel_api_t* api);
void fat_module_init(kernel_api_t* api);

void load_disk_modules(const char* dir_path);

/* Timer ISR dispatch — called from timer_isr.asm, routes to registered module handler */
void timer_isr_dispatch(context_t* frame);

/* Mouse wrapper functions used by kernel/gui.c and kernel/winman.c */
int mouse_get_x_wrapper(void);
int mouse_get_y_wrapper(void);
uint8_t mouse_get_buttons_wrapper(void);
int mouse_is_present_wrapper(void);

#endif
