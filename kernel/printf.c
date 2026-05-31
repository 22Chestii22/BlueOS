#include "types.h"
#include "screen.h"

void printf(const char* fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    for (int i = 0; fmt[i]; i++)
    {
        if (fmt[i] != '%' || fmt[i + 1] == '%')
        {
            if (fmt[i] == '%' && fmt[i + 1] == '%')
                i++;
            screen_putchar(fmt[i]);
            continue;
        }

        char pad = ' ';
        int width = 0;
        int left_align = 0;

        i++;
        if (fmt[i] == '-')
        {
            left_align = 1;
            i++;
        }
        if (fmt[i] == '0')
        {
            pad = '0';
            i++;
        }
        while (fmt[i] >= '0' && fmt[i] <= '9')
        {
            width = width * 10 + (fmt[i] - '0');
            i++;
        }
        if (fmt[i] == '.')
        {
            i++;
            while (fmt[i] >= '0' && fmt[i] <= '9')
                i++;
        }

        char buf[32];
        int bi = 0;

        switch (fmt[i])
        {
            case 'd':
            case 'i':
                {
                    int64_t val = __builtin_va_arg(args, int64_t);
                    if (val < 0)
                    {
                        buf[bi++] = '-';
                        val = -val;
                    }
                    char tmp[32];
                    int ti = 0;
                    if (val == 0) tmp[ti++] = '0';
                    while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; }
                    while (ti > 0) buf[bi++] = tmp[--ti];
                }
                {
                    int printed = bi;
                    if (!left_align)
                        while (printed < width) { screen_putchar(pad); printed++; }
                    for (int j = 0; j < bi; j++) screen_putchar(buf[j]);
                    if (left_align)
                        while (printed < width) { screen_putchar(' '); printed++; }
                }
                break;

            case 'u':
                {
                    uint64_t uv = __builtin_va_arg(args, uint64_t);
                    char tmp[32];
                    int ti = 0;
                    if (uv == 0) tmp[ti++] = '0';
                    while (uv > 0) { tmp[ti++] = '0' + (uv % 10); uv /= 10; }
                    bi = ti;
                    int printed = ti;
                    if (!left_align)
                        while (printed < width) { screen_putchar(pad); printed++; }
                    while (ti > 0) screen_putchar(tmp[--ti]);
                    if (left_align)
                        while (printed < width) { screen_putchar(' '); printed++; }
                }
                break;

            case 'x':
            case 'X':
                {
                    uint64_t uv = __builtin_va_arg(args, uint64_t);
                    const char* hex = (fmt[i] == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    char tmp[32];
                    int ti = 0;
                    if (uv == 0) tmp[ti++] = '0';
                    while (uv > 0) { tmp[ti++] = hex[uv & 0xF]; uv >>= 4; }
                    bi = ti;
                    int printed = ti;
                    if (!left_align)
                        while (printed < width) { screen_putchar(pad); printed++; }
                    while (ti > 0) screen_putchar(tmp[--ti]);
                    if (left_align)
                        while (printed < width) { screen_putchar(' '); printed++; }
                }
                break;

            case 's':
                {
                    const char* str = __builtin_va_arg(args, const char*);
                    if (!str) str = "(null)";
                    int len = 0;
                    while (str[len]) len++;
                    int printed = len;
                    if (!left_align)
                        while (printed < width) { screen_putchar(pad); printed++; }
                    screen_write(str);
                    if (left_align)
                        while (printed < width) { screen_putchar(' '); printed++; }
                }
                break;

            case 'c':
                {
                    int64_t val = __builtin_va_arg(args, int64_t);
                    int printed = 1;
                    if (!left_align)
                        while (printed < width) { screen_putchar(pad); printed++; }
                    screen_putchar((char)val);
                    if (left_align)
                        while (printed < width) { screen_putchar(' '); printed++; }
                }
                break;

            case 'p':
                {
                    void* ptr = __builtin_va_arg(args, void*);
                    screen_write("0x");
                    const char* hex = "0123456789ABCDEF";
                    char tmp[32];
                    int ti = 0;
                    uint64_t uv = (uint64_t)ptr;
                    if (uv == 0) tmp[ti++] = '0';
                    while (uv > 0) { tmp[ti++] = hex[uv & 0xF]; uv >>= 4; }
                    while (ti > 0) screen_putchar(tmp[--ti]);
                }
                break;

            default:
                screen_putchar('%');
                screen_putchar(fmt[i]);
                break;
        }
    }

    __builtin_va_end(args);
}
