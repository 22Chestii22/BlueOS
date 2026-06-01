#include "types.h"
#include "kernel_api.h"

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
static volatile int mouse_init_done = 0;

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

static int mouse_write(uint8_t data)
{
    mouse_wait_write();
    api->outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait_write();
    api->outb(MOUSE_PORT_DATA, data);
    uint8_t ack = mouse_read();
    if (ack != MOUSE_ACK)
        return -1;
    return 0;
}

void mouse_handler(void)
{
    if (!mouse_init_done)
        return;

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
            if (api && (uint32_t)mouse_x >= api->fb_width) mouse_x = api->fb_width - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (api && (uint32_t)mouse_y >= api->fb_height) mouse_y = api->fb_height - 1;

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

    if (mouse_write(MOUSE_SET_DEFAULTS) != 0) return -1;
    if (mouse_write(MOUSE_SET_SAMPLE) != 0) return -1;
    if (mouse_write(100) != 0) return -1;
    if (mouse_write(MOUSE_ENABLE) != 0) return -1;

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

    mouse_init_done = 0;

    if (api->fb_width > 0 && api->fb_height > 0)
    {
        mouse_x = api->fb_width / 2;
        mouse_y = api->fb_height / 2;
    }

    api->irq_install_handler(12, (void*)mouse_handler);
    if (mouse_init_ps2() != 0)
    {
        api->printf("[MOUSE] PS/2 init failed\n");
        return;
    }

    mouse_init_done = 1;

    api->printf("[MOUSE] Module loaded (IRQ 12) at %dx%d\n", mouse_x, mouse_y);
}
