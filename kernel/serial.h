#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char* str);
int serial_read_char(void);
int serial_char_avail(void);
void serial_hex(uint64_t val);
void serial_dec(uint64_t val);

#endif
