#ifndef TIMER_H
#define TIMER_H

#include "types.h"
#include "process.h"

void timer_handler_and_schedule(context_t* frame);
void timer_handler(void);
uint64_t timer_get_ticks(void);
void timer_sleep(uint64_t ms);
void timer_init(int frequency);
void timer_scheduler_enable(void);

#endif
