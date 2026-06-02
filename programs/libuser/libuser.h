#ifndef LIBUSER_H
#define LIBUSER_H

#include "types.h"
#include "syscall.h"

// GUI Event
typedef struct {
    int type;
    int mx, my;
    int buttons;
} gui_event_t;

// Standard Library & Process
extern int cmd_win;
void exit(int code);
void* malloc(uint32_t size);
void free(void* ptr);
void yield(void);
uint32_t get_pid(void);
void sleep(uint64_t ms);

// Terminal / GUI Console output
void puts(const char* s);
void putchar(char c);
void printf(const char* fmt, ...);

// VFS
int open(const char* path, int flags);
int read(int fd, void* buf, uint32_t size);
int write(int fd, const void* buf, uint32_t size);
int close(int fd);
int readdir(const char* path, char* entries, int max_entries);
int exists(const char* path);
int pe_check(const char* path);
int exec_wait(const char* path);

// GUI System
int gui_create(const char* title, int w, int h);
int gui_create_terminal(const char* title, int w, int h);
void gui_clear_terminal(void);
void gui_puts(int win, const char* str);
void gui_putchar(int win, char c);
void gui_clear(int win);
void gui_set_title(int win, const char* title);
void gui_get_window_rect(int win, int* x, int* y, int* w, int* h);
int gui_get_event(int win, gui_event_t* ev);
void gui_render(void);
void gui_draw_rect(int win, int x, int y, int w, int h, uint32_t color);
void gui_draw_text(int win, int x, int y, const char* str, uint32_t fg, uint32_t bg);

// Keyboard
int getchar(void);
int key_avail(void);

// String/Memory
size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strchr(const char* str, int c);
char* strstr(const char* haystack, const char* needle);
void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
int memcmp(const void* ptr1, const void* ptr2, size_t num);

#endif
