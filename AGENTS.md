# BlueOS — Agent Guide

## Project Overview

x86_64 hobby OS with:
- Multiboot2 boot (GRUB → kernel.elf)
- FAT32 filesystem on secondary disk (superfloppy, no MBR)
- PE32+ executable loader (Windows-style .exe)
- Preemptive multitasking (timer IRQ0, ring 3 user mode)
- Module system: drivers as .c files in `modules/`, linked into kernel.elf (phase 1), eventually loadable from `/SYSTEM/DRIVERS/` (phase 2)

## Architecture

```
kernel/           — core kernel (IDT, GDT, paging, process, scheduler, VFS, syscalls, cmd)
modules/          — pluggable driver modules
  keyb/             keyboard driver (IRQ 1 via api->inb/outb)
  timer/            PIT + scheduler (IRQ 0 via api->outb, process calls directly)
  ata/              ATA PIO disk driver
  fat/              FAT32 filesystem driver
programs/         — user-mode PE32+ executables
scripts/          — build_image.sh (FAT32 disk with mtools)
```

## Module System

Each module has an init function: `void X_module_init(kernel_api_t* api)`.

The `kernel_api_t` struct (kernel/kernel_api.h) provides function pointers for:
- printf, screen I/O
- malloc/free, memcpy/memset
- string functions (strcmp, strcpy, strlen, etc.)
- Port I/O (inb/outb/inw/outw/inl/outl)
- irq_install_handler
- ata_read_sectors / ata_write_sectors
- register_keyb_getchar / register_timer_get_ticks

Modules can also call kernel functions directly (statically linked in phase 1).

## Build

```bash
make clean && make -j$(nproc)
make run   # QEMU
make debug # QEMU with cpu_reset logging
```

Outputs: `blueos.iso` (bootable CD), `disk.img` (FAT32 data disk).

## What's Been Done

### Session 1 (Initial)
- GitHub repo: https://github.com/22Chestii22/BlueOS
- Superfloppy disk image (VBR at LBA 0, no MBR partition table)
- Bugfix: `cd .` shows current directory
- Bugfix: `dir` works with superfloppy
- kernel_api.h / module.h / module.c — kernel API infrastructure

### Session 2
- KEYB module: keyboard driver using api->inb/outb, registers IRQ 1
- TIMER module: PIT driver + scheduler using api->outb, registers IRQ 0
- Removed kernel/keyb.c, kernel/timer.c from kernel build
- Fixed modules/timer/timer.c missing includes (paging.h, gdt.h)
- Fixed process_wait() — calls schedule() instead of busy-loop
- Fixed process_exit() — calls schedule() before infinite hlt
- Removed EDIT (non-functional program)
- Restructured /SYSTEM/ (AUTOEXEC.BAT + CONFIG.SYS + programs all in /SYSTEM/)
- cmd.c PATH defaults to \SYSTEM, searches \SYSTEM\ for executables

### Session 3
- Created user-mode Scout file explorer (`programs/scout/scout.asm`, PE32+)
- Fixed `paging_create_pml4()` and `paging_map_user()` — PML4/PDPT entries
  now use `|= 0x06` (User|Write) instead of `|= 0x04` (User only). Without
  Write bit at every level, user-mode writes cause #PF with error 0x7.
- Fixed `lea rdi, [rel X + rax]` bug (3 instances in scout.asm). NASM warns
  "indirect address displacements cannot be RIP-relative" and falls back to
  using the binary file offset instead of the virtual address — causing #PF
  when the computed address falls in the identity-mapped 2MB page (no User
  bit). Fix: split into `lea rdi, [rel X]` + `add rdi, rax`.
- Fixed timer handler register save mapping — `ctx[i] = frm[14-i]` (timer
  ISR pushes regs in rax..r15 order, context_t expects r15..rax).
- Created `yield_from_user_syscall` in `kernel/switch.asm` for syscall 28
  (user-mode yield). Builds proper user frame: CS=0x23, SS=0x1B, user RIP
  from rcx, user RFLAGS from r11, user RSP from gs:0x10.
- Routed syscall 28 in `kernel/syscall_asm.asm` — intercepts rax=28 BEFORE
  `handle_syscall` dispatch, calls `yield_from_user_syscall` directly.
- Verified: Scout runs stably with no exceptions in QEMU (GUI renders,
  mouse clicks handled, no #PF or #GP).

## How NOT to Break Things

1. **Superfloppy constraint**: disk.img must be FAT32 with VBR at LBA 0. NO MBR partition table. Kernel reads LBA 0 directly.
2. **PE loader**: PE32+ only (64-bit). Programs at 0x400000 image base, user stack at 0x70000000.
3. **Timer ISR**: IDT entry 32 is hardwired to timer_isr → timer_handler_and_schedule. Do NOT use irq_install_handler(0, ...) for timer — it goes through the assembly wrapper directly.
4. **Ring 0 preemption**: Timer ISR skips scheduling when interrupted in ring 0 (`if ((frame->cs & 3) == 0) return`). Ring 0 code must voluntarily call `schedule()` for context switches.
5. **process_exit flow**: process_exit() calls schedule() to hand control to next process. The `for(;;) hlt` is a fallback if no processes remain.
6. **Module includes**: Module .c files in `modules/` use `-I. -I./kernel` for includes. They can include kernel headers for now but should prefer api-> calls.
7. **disk.img**: format with `mformat -i disk.img -F ::` (superfloppy mode). mtools preferred over sudo+mount.
8. **`[rel X + rax]` addressing**: AVOID in NASM. This affects `lea rdi, [rel X + rax]` AND `mov [rel X + rax], 0`. Always split into `lea rdi, [rel X]` + `add rdi, rax` (or for MOV: `lea rdi, [rel X]` + `add rdi, rax` + `mov [rdi], 0`). NASM generates a warning and falls back to binary offset which hits identity-mapped pages (no User bit), causing #PF.
9. **PML4 page table entry permissions**: User-mode pages MUST have Write bit (0x02) set at EVERY paging level (PML4, PDPT, PD, PT). Setting only User bit (0x04) causes #PF with error 0x7 on writes.
10. **Timer ISR register order**: Timer ISR pushes registers in rax..r15 order (idx 0..14). context_t expects r15..rax (idx 0..14). Mapping is `ctx[i] = frm[14-i]`.
11. **User-mode syscall for yield**: Don't call `yield_to_scheduler` from user mode (it pushes ring 0 CS/SS). Use syscall 28 which triggers `yield_from_user_syscall` with proper user frame.

## Files Requiring Careful Edits

| File | Why |
|---|---|
| kernel/main.c | Init order matters. Must NOT break existing sequence. |
| kernel/module.c | kernel_api struct with typed function pointers — casts for memcpy/memset are intentional (size mismatch). |
| kernel/process.c | Core lifecycle. process_wait/process_exit/schedule interaction is subtle. |
| kernel/linker.ld | Section layout. MUST NOT change without understanding multiboot2/GRUB constraints. |
| scripts/build_image.sh | Superfloppy format. Must NOT create MBR. |
| kernel/idt.c | IDT entry 32 = timer_isr hardcoded. PIC remap must stay. |
| programs/scout/scout.asm | User-mode PE32+ program. Avoid `lea rdi,[rel X+rax]` pattern. Follow syscall ABI. |
| programs/cmd/cmd.asm | CMD shell. Also has `lea rdi,[rel X+rax]` pattern (10 instances fixed 2026-06-01). |

### Session 4

- Fixed **10 `lea rdi,[rel X+rax]` bugs in cmd.asm** — same exact bug pattern as scout.asm. These caused #GP when CMD navigated directories or searched for executables (every `strlen` + offset pattern and the `mov [rel ext_path_buf+rax],0` line). Build now shows zero RIP-relative warnings.
- Fixed **mouse driver ACK verification** — `mouse_write()` now checks that the ACK byte equals 0xFA and returns -1 on failure. `mouse_init_ps2()` propagates failures. `mouse_module_init()` prints `[MOUSE] PS/2 init failed` and doesn't set `mouse_init_done` on failure (handler stays disabled).
- Fixed **gdt_set_kernel_stack in process.c** — removed the call inside the `if (user)` block of `process_create()`. It was setting TSS RSP0 to the new process's kernel stack during creation, which could corrupt the kernel stack pointer if a timer IRQ or context switch fired during creation. TSS RSP0 is correctly set by `yield_handler()` and `main.c` activation code.

### Session 5

- Renamed **GUI_RENDER.EXE → RENDER.EXE** (8.3 FAT32 compatibility). FAT driver uses 8.3 short names internally; "GUI_RENDER" (10 chars) was truncated to "GUI_REND" and never matched `vfs_open("\SYSTEM\PROGRAMS\GUI_RENDER.EXE")`. Changed: `build_image.sh` mcopy target, `main.c` pe_spawn path, `Makefile` target name.
- **Deep-copy page tables resolve #DF**: The `paging_create_pml4()` deep copy in `paging.c` (Session 4) eliminates the #DF at 0x101790 by giving each process independent (non-shared) page table hierarchies. Verified: 3 processes (RENDER, SCOUT, IDLE) boot cleanly with no exceptions in QEMU, all in ring 3 with proper CR3 switching.
- **JUMP_SLOT relocation fix for KEYB.SYS/MOUSE.SYS**: Added `R_X86_64_JUMP_SLOT` handler and `JMPREL` (`.rela.plt`) processing in `elf_loader.c`. Without this, `-fPIC`-compiled modules crash with #UD when calling global functions because the unpatched GOT.PLT entry points to lazy binding stub → jumps to address 0.
- **Timer enabled**: `timer_start()` and `timer_scheduler_enable()` are called from `main.c`. PIT is programmed at 100 Hz. Preemptive multitasking is active — timer IRQ context switches between RENDER, CMD, and IDLE.
- **Fixed swapgs imbalance causing Double Fault on SYSCALL after context switch** (`kernel/switch.asm`): When `yield_from_user_syscall` → `yield_handler` → `context_activate` switched to a new process, the `swapgs` done at SYSCALL entry was never undone (because `iretq` bypasses the balancing `swapgs` + `sysretq`). The new process ran with `GS.base=&cpu_data` (kernel base). Its subsequent `swapgs` at SYSCALL entry swapped back to `MSR_GS_BASE=0`, causing `mov rsp, gs:0x18` to read from physical address 0x18 instead of `cpu_data[3]` → #PF → #DF.
      - Fix: `context_activate.ring3` now explicitly sets `GS.base=0` and `MSR_KERNEL_GS_BASE=&cpu_data` via WRMSR before `iretq`. This works for ALL entry paths (SYSCALL yield, timer IRQ, first activation).

### Session 6

- **Fixed #DF on keyboard input** — two bugs in ring 0 context switch caused #PF→#DF when typing in CMD terminal:
  1. **context_activate.ring0 missed GS.base restore** (`kernel/switch.asm`): When a process yielded from inside a syscall (e.g., GETCHAR via `keyb_getchar` → `yield_to_scheduler`), the saved context had CS=0x08 (ring 0). The timer IRQ later resumed this process via `context_activate.ring0`, which did NOT set GS.base. GS.base was still 0 from the prior `.ring3` activation (set for a user process). The syscall exit path then executed `mov rsp, gs:0x10`, reading from physical address 0x10 → #PF → #DF. Fix: `.ring0` now sets `GS.base=&cpu_data` and `MSR_KERNEL_GS_BASE=0` via WRMSR, matching the swapgs-at-syscall-entry state.
  2. **cpu_data[2] user RSP corruption** (`kernel/process.c`, `modules/timer/timer.c`): `cpu_data[2]` (written by `mov gs:0x10, rsp` at syscall entry) is a single global slot. When another process issued a syscall between the yield and resume, it overwrote `cpu_data[2]` with its own user RSP. The resumed process then loaded the wrong stack pointer on syscall exit. Fix: added `user_rsp` field to `process_t`, saved in `yield_handler` and `timer_handler_and_schedule`, restored into `cpu_data[2]` before `context_activate`.

### Session 7

- **Fixed `strlen` register bug in cmd.asm `dir` command** (`programs/cmd/cmd.asm`): The `dir` parsing loop called `strlen` with string pointer in `r14` but `strlen` reads from `rdi`. Since `rdi` at that point held `cmd_win` (a small integer from the GUI window handle, e.g. 0), strlen read from virtual address 0 instead of the directory entry name, returning wrong length (0). This caused the loop to advance by incorrect byte counts, skipping entries and only processing 1 entry before hitting a null byte. All subdirectory listings (`dir \SYSTEM`, `dir` in current dir) showed count=1.
  - Fix: `lea rdi, [r14 + 2]` before calling `strlen` instead of `add r14, 2`.
- **Restored dir entry display**: The committed code was replaced with a counting-only loop (no visible output). Restored the full display showing each entry (`DIRNAME\` for dirs, filenames for files) plus file/dir counts.
- **yield_to_scheduler frame base fix** (`kernel/switch.asm`): Fixed RSP computation for ring 0 yield frame to use correct frame base address.
- **yield_from_user_syscall** (`kernel/switch.asm`): Reads user RSP from `gs:0x10` directly without swapgs (fixes potential GS.base corruption).
- **process.c**: Removed debug printf spam from `process_create`. Added `proc->user_rsp` initialization for user-mode processes.
- **syscall.c**: Added serial debug output for readdir syscall.
- **gui.c**: Added serial debug output for `gui_puts`.
- **Added auto-exec `dir` on CMD startup** for testing.

## Commit & Release Rules

After every successful update that compiles and makes sense:
1. `git add -A`
2. `git commit -m "descriptive message in English, present tense"`
3. `git push`
4. If the commit is a significant milestone or stable working state, create a tag:
   - `git tag -a vX.Y.Z -m "vX.Y.Z: summary"`
   - `git push origin vX.Y.Z`
5. If a GitHub release should be created: `gh release create vX.Y.Z --title "vX.Y.Z" --notes "<summary>"`

Commit messages: imperative mood, e.g. "Add ATA module", "Fix process_wait scheduling", "Restructure /SYSTEM/ directory".
Release tags: follow semver, bump minor for features, patch for bugfixes.
