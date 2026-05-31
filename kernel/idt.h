#ifndef IDT_H
#define IDT_H

#include "types.h"

void idt_init(void);
void isr_handler(int num, uint64_t error_code);
void irq_handler(int num);
void irq_install_handler(int irq, void (*handler)(void));

#endif
