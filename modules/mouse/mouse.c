#include "types.h"
#include "kernel_api.h"
#include "vga.h"

static kernel_api_t* api = NULL;

#define MOUSE_PORT_DATA   0x60
#define MOUSE_PORT_CMD    0x64

#define MOUSE_ACK         0xFA
#define MOUSE_ENABLE      0xF4
#define MOUSE_DISABLE     0xF5
#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_SET_SAMPLE  0xF3
#define MOUSE_GET_INFO    0xE9

static volatile int mouse_x = 400;
static volatile int mouse_y = 300;
static volatile uint8_t mouse_buttons = 0;
static volatile int mouse_present = 0;

static volatile int mouse_packet_index = 0;
static volatile uint8_t mouse_packet[3];

static void mouse_wait_write(void)
{
    for (int i = 0; i < 100000; i++)
    {
        if (!(api->inb(MOUSE_PORT_CMD) & 2)) return;
        api->inb(0x80);
    }
}

static void mouse_wait_read(void)
{
    for (int i = 0; i < 100000; i++)
    {
        if (api->inb(MOUSE_PORT_CMD) & 1) return;
        api->inb(0x80);
    }
}

static uint8_t mouse_read(void)
{
    mouse_wait_read();
    return api->inb(MOUSE_PORT_DATA);
}

static void mouse_write(uint8_t data)
{
    mouse_wait_write();
    api->outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait_write();
    api->outb(MOUSE_PORT_DATA, data);
    mouse_read();
}

void mouse_handler(void)
{
    uint8_t status = api->inb(MOUSE_PORT_CMD);
    if (!(status & 0x20)) return;
    if (!(status & 1)) return;

    uint8_t data = api->inb(MOUSE_PORT_DATA);

    switch (mouse_packet_index)
    {
        case 0:
            mouse_packet[0] = data;
            if (!(data & 0x08))
            {
                mouse_packet_index = 0;
                return;
            }
            mouse_packet_index = 1;
            break;
        case 1:
            mouse_packet[1] = data;
            mouse_packet_index = 2;
            break;
        case 2:
            mouse_packet[2] = data;

            mouse_buttons = mouse_packet[0] & 0x07;

            int dx = (int)(int8_t)mouse_packet[1];
            int dy = -(int)(int8_t)mouse_packet[2];

            mouse_x += dx;
            mouse_y += dy;

            if (mouse_x < 0) mouse_x = 0;
            if ((uint32_t)mouse_x >= (uint32_t)vga_get_mode_width()) mouse_x = vga_get_mode_width() - 1;
            if (mouse_y < 0) mouse_y = 0;
            if ((uint32_t)mouse_y >= (uint32_t)vga_get_mode_height()) mouse_y = vga_get_mode_height() - 1;

            mouse_packet_index = 0;
            break;
    }
}

static int mouse_init_ps2(void)
{
    api->outb(MOUSE_PORT_CMD, 0xA8);
    api->inb(0x80);

    mouse_wait_write();
    api->outb(MOUSE_PORT_CMD, 0x20);
    uint8_t config = mouse_read();
    config |= 0x02;
    mouse_wait_write();
    api->outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait_write();
    api->outb(MOUSE_PORT_DATA, config);

    mouse_write(MOUSE_SET_DEFAULTS);
    mouse_write(MOUSE_SET_SAMPLE);
    mouse_write(100);
    mouse_write(MOUSE_ENABLE);

    mouse_present = 1;
    return 0;
}

int mouse_get_x(void)
{
    return mouse_x;
}

int mouse_get_y(void)
{
    return mouse_y;
}

uint8_t mouse_get_buttons(void)
{
    return mouse_buttons;
}

int mouse_is_present(void)
{
    return mouse_present;
}

void mouse_module_init(kernel_api_t* kapi)
{
    api = kapi;

    api->irq_install_handler(12, (void*)mouse_handler);
    mouse_init_ps2();

    api->printf("[MOUSE] Module loaded (IRQ 12)\n");
}
