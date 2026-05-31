#include "types.h"
#include "kernel_api.h"

#define KEYB_PORT 0x60
#define KEYB_STATUS 0x64

#define KEYB_EXTENDED 0xE0

static kernel_api_t* api = NULL;

static volatile char key_buffer[256];
static volatile int key_buffer_head = 0;
static volatile int key_buffer_tail = 0;
static volatile int shift_pressed = 0;
static volatile int caps_lock = 0;
static volatile int extended = 0;

static const char scancode_ascii[] = {
    [0x00] = 0,  [0x01] = 0x1B, [0x02] = '1',   [0x03] = '2',
    [0x04] = '3',   [0x05] = '4',   [0x06] = '5',   [0x07] = '6',
    [0x08] = '7',   [0x09] = '8',   [0x0A] = '9',   [0x0B] = '0',
    [0x0C] = '-',   [0x0D] = '=',   [0x0E] = '\b',  [0x0F] = '\t',
    [0x10] = 'q',   [0x11] = 'w',   [0x12] = 'e',   [0x13] = 'r',
    [0x14] = 't',   [0x15] = 'y',   [0x16] = 'u',   [0x17] = 'i',
    [0x18] = 'o',   [0x19] = 'p',   [0x1A] = '[',   [0x1B] = ']',
    [0x1C] = '\n',  [0x1D] = 0,     [0x1E] = 'a',   [0x1F] = 's',
    [0x20] = 'd',   [0x21] = 'f',   [0x22] = 'g',   [0x23] = 'h',
    [0x24] = 'j',   [0x25] = 'k',   [0x26] = 'l',   [0x27] = ';',
    [0x28] = '\'',  [0x29] = '`',   [0x2A] = 0,     [0x2B] = '\\',
    [0x2C] = 'z',   [0x2D] = 'x',   [0x2E] = 'c',   [0x2F] = 'v',
    [0x30] = 'b',   [0x31] = 'n',   [0x32] = 'm',   [0x33] = ',',
    [0x34] = '.',   [0x35] = '/',   [0x36] = 0,     [0x37] = '*',
    [0x38] = 0,     [0x39] = ' ',   [0x3A] = 0,     [0x3B] = 0,
    [0x3C] = 0,     [0x3D] = 0,     [0x3E] = 0,     [0x3F] = 0,
    [0x40] = 0,     [0x41] = 0,     [0x42] = 0,     [0x43] = 0,
    [0x44] = 0,     [0x45] = 0,     [0x46] = 0,     [0x47] = 0,
    [0x48] = 0,     [0x49] = 0,     [0x4A] = '-',   [0x4B] = 0,
    [0x4C] = 0,     [0x4D] = 0,     [0x4E] = '+',   [0x4F] = 0,
    [0x50] = 0,     [0x51] = 0,     [0x52] = 0,     [0x53] = 0,
    [0x57] = 0,     [0x58] = 0,
};

static const char scancode_shift[] = {
    [0x00] = 0,  [0x01] = 0x1B, [0x02] = '!',   [0x03] = '@',
    [0x04] = '#',   [0x05] = '$',   [0x06] = '%',   [0x07] = '^',
    [0x08] = '&',   [0x09] = '*',   [0x0A] = '(',   [0x0B] = ')',
    [0x0C] = '_',   [0x0D] = '+',   [0x0E] = '\b',  [0x0F] = '\t',
    [0x10] = 'Q',   [0x11] = 'W',   [0x12] = 'E',   [0x13] = 'R',
    [0x14] = 'T',   [0x15] = 'Y',   [0x16] = 'U',   [0x17] = 'I',
    [0x18] = 'O',   [0x19] = 'P',   [0x1A] = '{',   [0x1B] = '}',
    [0x1C] = '\n',  [0x1D] = 0,     [0x1E] = 'A',   [0x1F] = 'S',
    [0x20] = 'D',   [0x21] = 'F',   [0x22] = 'G',   [0x23] = 'H',
    [0x24] = 'J',   [0x25] = 'K',   [0x26] = 'L',   [0x27] = ':',
    [0x28] = '"',   [0x29] = '~',   [0x2A] = 0,     [0x2B] = '|',
    [0x2C] = 'Z',   [0x2D] = 'X',   [0x2E] = 'C',   [0x2F] = 'V',
    [0x30] = 'B',   [0x31] = 'N',   [0x32] = 'M',   [0x33] = '<',
    [0x34] = '>',   [0x35] = '?',   [0x36] = 0,     [0x37] = '*',
    [0x38] = 0,     [0x39] = ' ',   [0x3A] = 0,     [0x3B] = 0,
    [0x3C] = 0,     [0x3D] = 0,     [0x3E] = 0,     [0x3F] = 0,
    [0x40] = 0,     [0x41] = 0,     [0x42] = 0,     [0x43] = 0,
    [0x44] = 0,     [0x45] = 0,     [0x46] = 0,     [0x47] = 0,
    [0x48] = 0,     [0x49] = 0,     [0x4A] = '-',   [0x4B] = 0,
    [0x4C] = 0,     [0x4D] = 0,     [0x4E] = '+',   [0x4F] = 0,
    [0x50] = 0,     [0x51] = 0,     [0x52] = 0,     [0x53] = 0,
    [0x57] = 0,     [0x58] = 0,
};

void keyboard_handler(void)
{
    uint8_t scancode = api->inb(KEYB_PORT);

    if (extended)
    {
        extended = 0;
        if (scancode & 0x80) return;
        return;
    }

    if (scancode == KEYB_EXTENDED)
    {
        extended = 1;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36)
        shift_pressed = 1;
    else if (scancode == 0xAA || scancode == 0xB6)
        shift_pressed = 0;
    else if (scancode == 0x3A)
        caps_lock = !caps_lock;

    if (scancode & 0x80) return;

    char c = 0;
    if (scancode < sizeof(scancode_ascii))
    {
        if (shift_pressed || caps_lock)
            c = scancode_shift[scancode];
        else
            c = scancode_ascii[scancode];
    }

    if (c)
    {
        int next = (key_buffer_head + 1) % 256;
        if (next != key_buffer_tail)
        {
            key_buffer[key_buffer_head] = c;
            key_buffer_head = next;
        }
    }
}

char keyb_getchar(void)
{
    while (key_buffer_head == key_buffer_tail)
        __asm__ volatile("pause");

    char c = key_buffer[key_buffer_tail];
    key_buffer_tail = (key_buffer_tail + 1) % 256;
    return c;
}

int keyb_char_avail(void)
{
    return key_buffer_head != key_buffer_tail;
}

void keyb_module_init(kernel_api_t* kapi)
{
    api = kapi;
    api->irq_install_handler(1, (void*)keyboard_handler);
    api->printf("[KEYB] Module loaded\n");
}
