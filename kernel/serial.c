#include "types.h"
#include "io.h"

#define SERIAL_PORT 0x3F8

static int serial_initialized = 0;

void serial_init(void)
{
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
    serial_initialized = 1;
}

static int serial_received(void)
{
    return inb(SERIAL_PORT + 5) & 1;
}

static int serial_is_transmit_empty(void)
{
    return inb(SERIAL_PORT + 5) & 0x20;
}

void serial_write_char(char c)
{
    if (!serial_initialized) return;

    while (!serial_is_transmit_empty());

    if (c == '\n')
    {
        outb(SERIAL_PORT, '\r');
        while (!serial_is_transmit_empty());
    }

    outb(SERIAL_PORT, c);
}

void serial_write(const char* str)
{
    while (*str)
        serial_write_char(*str++);
}

int serial_read_char(void)
{
    if (!serial_initialized) return -1;
    if (!serial_received()) return -1;
    return inb(SERIAL_PORT);
}

int serial_char_avail(void)
{
    if (!serial_initialized) return 0;
    return serial_received() ? 1 : 0;
}

void serial_hex(uint64_t val)
{
    const char* hex = "0123456789ABCDEF";
    char buf[17];
    buf[16] = 0;
    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    serial_write(buf);
}

void serial_dec(uint64_t val)
{
    char buf[21];
    int i = 20;
    buf[20] = 0;
    if (val == 0) { serial_write("0"); return; }
    while (val > 0 && i > 0)
    {
        i--;
        buf[i] = '0' + (val % 10);
        val /= 10;
    }
    serial_write(buf + i);
}
