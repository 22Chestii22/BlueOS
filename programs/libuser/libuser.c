#include "libuser.h"

int cmd_win = -1;

void exit(int code)
{
    __syscall1(SYSCALL_EXIT, code);
    for (;;);
}

void* malloc(uint32_t size)
{
    return (void*)__syscall1(SYSCALL_MALLOC, size);
}

void free(void* ptr)
{
    __syscall1(SYSCALL_FREE, (uint64_t)ptr);
}

void yield(void)
{
    __syscall0(SYSCALL_YIELD);
}

uint32_t get_pid(void)
{
    return (uint32_t)__syscall0(SYSCALL_GETPID);
}

void sleep(uint64_t ms)
{
    __syscall1(SYSCALL_SLEEP, ms);
}

void kernel_puts(const char* s)
{
    __syscall1(SYSCALL_PUTS, (uint64_t)s);
}

void puts(const char* s)
{
    if (cmd_win >= 0)
        gui_puts(cmd_win, s);
    else
        kernel_puts(s);
}

void putchar(char c)
{
    if (cmd_win >= 0)
        gui_putchar(cmd_win, c);
    else
    {
        char buf[2] = {c, 0};
        kernel_puts(buf);
    }
}

int open(const char* path, int flags)
{
    return (int)__syscall2(SYSCALL_OPEN, (uint64_t)path, flags);
}

int read(int fd, void* buf, uint32_t size)
{
    return (int)__syscall3(SYSCALL_READ, fd, (uint64_t)buf, size);
}

int write(int fd, const void* buf, uint32_t size)
{
    return (int)__syscall3(SYSCALL_WRITE, fd, (uint64_t)buf, size);
}

int close(int fd)
{
    return (int)__syscall1(SYSCALL_CLOSE, fd);
}

int readdir(const char* path, char* entries, int max_entries)
{
    return (int)__syscall3(SYSCALL_READDIR, (uint64_t)path, (uint64_t)entries, max_entries);
}

int exists(const char* path)
{
    return (int)__syscall1(SYSCALL_EXISTS, (uint64_t)path);
}

int pe_check(const char* path)
{
    return (int)__syscall1(SYSCALL_PE_CHECK, (uint64_t)path);
}

int exec_wait(const char* path)
{
    return (int)__syscall1(SYSCALL_EXEC_WAIT, (uint64_t)path);
}

int gui_create(const char* title, int w, int h)
{
    return (int)__syscall3(SYSCALL_GUI_CREATE, (uint64_t)title, w, h);
}

int gui_create_terminal(const char* title, int w, int h)
{
    return (int)__syscall5(SYSCALL_CREATE_TERM, (uint64_t)title, 30, 30, w, h);
}

void gui_clear_terminal(void)
{
    __syscall0(SYSCALL_CLR_TERM);
}

void gui_puts(int win, const char* str)
{
    __syscall2(SYSCALL_GUI_PUTS, win, (uint64_t)str);
}

void gui_putchar(int win, char c)
{
    __syscall2(SYSCALL_GUI_PUTCHAR, win, c);
}

void gui_clear(int win)
{
    __syscall1(SYSCALL_GUI_CLEAR, win);
}

void gui_set_title(int win, const char* title)
{
    __syscall2(SYSCALL_GUI_TITLE, win, (uint64_t)title);
}

void gui_get_window_rect(int win, int* x, int* y, int* w, int* h)
{
    int rect[4] = {0};
    __syscall2(SYSCALL_GUI_RECT, win, (uint64_t)rect);
    if (x) *x = rect[0];
    if (y) *y = rect[1];
    if (w) *w = rect[2];
    if (h) *h = rect[3];
}

int gui_get_event(int win, gui_event_t* ev)
{
    return (int)__syscall2(SYSCALL_GUI_EVENT, win, (uint64_t)ev);
}

void gui_render(void)
{
    __syscall0(SYSCALL_GUI_RENDER);
}

void gui_draw_rect(int win, int x, int y, int w, int h, uint32_t color)
{
    __syscall6(SYSCALL_GUI_DRAW, win, x, y, w, h, color);
}

void gui_draw_text(int win, int x, int y, const char* str, uint32_t fg, uint32_t bg)
{
    __syscall6(SYSCALL_GUI_TEXT, win, x, y, (uint64_t)str, fg, bg);
}

int getchar(void)
{
    return (int)__syscall0(SYSCALL_GETCHAR);
}

int key_avail(void)
{
    return (int)__syscall0(SYSCALL_KEY_AVAIL);
}

// String/Memory Functions
size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dest, const char* src)
{
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = 0;
    return dest;
}

char* strcat(char* dest, const char* src)
{
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char* strchr(const char* str, int c)
{
    while (*str)
    {
        if (*str == (char)c) return (char*)str;
        str++;
    }
    return NULL;
}

char* strstr(const char* haystack, const char* needle)
{
    if (!*needle) return (char*)haystack;
    while (*haystack)
    {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

void* memset(void* ptr, int value, size_t num)
{
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++)
        p[i] = (unsigned char)value;
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t num)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < num; i++)
        d[i] = s[i];
    return dest;
}

int memcmp(const void* ptr1, const void* ptr2, size_t num)
{
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;
    for (size_t i = 0; i < num; i++)
    {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

// Printf implementation
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
            putchar(fmt[i]);
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
                        while (printed < width) { putchar(pad); printed++; }
                    for (int j = 0; j < bi; j++) putchar(buf[j]);
                    if (left_align)
                        while (printed < width) { putchar(' '); printed++; }
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
                        while (printed < width) { putchar(pad); printed++; }
                    while (ti > 0) putchar(tmp[--ti]);
                    if (left_align)
                        while (printed < width) { putchar(' '); printed++; }
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
                        while (printed < width) { putchar(pad); printed++; }
                    while (ti > 0) putchar(tmp[--ti]);
                    if (left_align)
                        while (printed < width) { putchar(' '); printed++; }
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
                        while (printed < width) { putchar(pad); printed++; }
                    puts(str);
                    if (left_align)
                        while (printed < width) { putchar(' '); printed++; }
                }
                break;

            case 'c':
                {
                    int64_t val = __builtin_va_arg(args, int64_t);
                    int printed = 1;
                    if (!left_align)
                        while (printed < width) { putchar(pad); printed++; }
                    putchar((char)val);
                    if (left_align)
                        while (printed < width) { putchar(' '); printed++; }
                }
                break;

            case 'p':
                {
                    void* ptr = __builtin_va_arg(args, void*);
                    puts("0x");
                    const char* hex = "0123456789ABCDEF";
                    char tmp[32];
                    int ti = 0;
                    uint64_t uv = (uint64_t)ptr;
                    if (uv == 0) tmp[ti++] = '0';
                    while (uv > 0) { tmp[ti++] = hex[uv & 0xF]; uv >>= 4; }
                    while (ti > 0) putchar(tmp[--ti]);
                }
                break;

            default:
                putchar('%');
                putchar(fmt[i]);
                break;
        }
    }

    __builtin_va_end(args);
}
