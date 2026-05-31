#ifndef SYSCALL_H
#define SYSCALL_H

void syscall_init(void);
int syscall_exec(uint64_t entry, const char* name);

#endif
