#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include "types.h"

#define SYSCALL_EXIT        1
#define SYSCALL_GETPID      2
#define SYSCALL_PUTS        3
#define SYSCALL_MALLOC      4
#define SYSCALL_FREE        5
#define SYSCALL_OPEN        6
#define SYSCALL_READ        7
#define SYSCALL_WRITE       8
#define SYSCALL_CLOSE       9
#define SYSCALL_GETCHAR     10
#define SYSCALL_CLR_SCR     11
#define SYSCALL_CREATE_TERM 14
#define SYSCALL_CLR_TERM    15
#define SYSCALL_READDIR     16
#define SYSCALL_PE_CHECK    17
#define SYSCALL_EXEC_WAIT   18
#define SYSCALL_EXISTS      19
#define SYSCALL_GUI_CREATE  20
#define SYSCALL_GUI_PUTS    22
#define SYSCALL_GUI_PUTCHAR 23
#define SYSCALL_GUI_CLEAR   24
#define SYSCALL_GUI_TITLE   25
#define SYSCALL_GUI_RECT    26
#define SYSCALL_GUI_EVENT   27
#define SYSCALL_YIELD       28
#define SYSCALL_GUI_RENDER  29
#define SYSCALL_GUI_DRAW    30
#define SYSCALL_GUI_TEXT    31
#define SYSCALL_KEY_AVAIL   32
#define SYSCALL_SLEEP       13

uint64_t __syscall0(uint64_t n);
uint64_t __syscall1(uint64_t n, uint64_t a1);
uint64_t __syscall2(uint64_t n, uint64_t a1, uint64_t a2);
uint64_t __syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3);
uint64_t __syscall4(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
uint64_t __syscall5(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
uint64_t __syscall6(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);

#endif
