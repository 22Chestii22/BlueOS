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
12. **`syscall` clobbers RCX and R11**: The `syscall` instruction writes RIP→RCX and RFLAGS→R11. NEVER pass 4th syscall arg (which should be in `r10`) in `rcx` — it will be destroyed. Always use `r10` for a4. Similarly, any value in RCX before `syscall` is lost.

## Files Requiring Careful Edits

| File | Why |
|---|---|
| kernel/main.c | Init order matters. Must NOT break existing sequence. |
| kernel/module.c | kernel_api struct with typed function pointers — casts for memcpy/memset are intentional (size mismatch). |
| kernel/process.c | Core lifecycle. process_wait/process_exit/schedule interaction is subtle. |
| kernel/linker.ld | Section layout. MUST NOT change without understanding multiboot2/GRUB constraints. |
| scripts/build_image.sh | Superfloppy format. Must NOT create MBR. |
| kernel/idt.c | IDT entry 32 = timer_isr hardcoded. PIC remap must stay. |
| programs/scout/scout.asm | User-mode PE32+ program. Avoid `lea rdi,[rel X+rax]` pattern. Follow syscall ABI — all 4th params in r10 (not rcx). No BSS section (must use `times N db 0` in .text). |
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

### Session 8

- **Fixed `dir` showing only 1 entry: RCX clobbered by `syscall` in `print_str`** (`programs/cmd/cmd.asm`): `print_str` uses raw `syscall` instruction which saves RIP into RCX. The `dir` loop expected RCX to hold `name_len` (from `strlen`) at `.dir_next` to calculate entry advancement. But all `print_str`/`print_crlf` calls between the calculation and `.dir_next` destroyed RCX, causing the advancement `r8 = rcx + rbx + 4` to use garbage → r14 overshot the buffer → next loop iteration's `cmp rbx, r15` exited immediately. Fix: `push rcx`/`push rbx` after computing name_len/size_len, `pop rbx`/`pop rcx` at `.dir_next`.
- **Root cause discovery**: The `syscall` instruction on x86-64 unconditionally writes RIP→RCX and RFLAGS→R11. Any wrapper function (like `print_str`) that does raw `syscall` without saving/restoring RCX will corrupt it — even though the value was in a "callee-saved" register from the caller's perspective. This is a silent, non-obvious ABI violation specific to raw `syscall` wrappers.
- **Cleaned up**: Removed all serial debug output (r15 dump, readdir hex dump) and `debug_star` that were added for this bug hunt.

### Session 9

- **Fixed Scout File Explorer bugs** (`programs/scout/scout.asm`):
  1. **BSS section → .text**: `make_pe.py` only creates a single `.text` section with `VirtualSize = RawSize`, so BSS variables get no allocated space → #PF on access. Moved all `section .bss` variable declarations to `.text` with `times N db 0` (same pattern as cmd.asm).
  2. **All `draw_text` syscall ABI fixes**: `syscall` clobbers `rcx` with RIP, but all 5 `gui_draw_text` calls passed the string pointer in `rcx` instead of `r10` (the correct 4th arg register for syscalls). Changed all `mov rcx, [rel X]` (or lea) to `lea r10, [rel X]`.
  3. **r13 used as both strlen temp AND dir counter**: `mov r13, rax` (strlen result) overwrote the persistent dir entry counter. Also, `inc r13d` appeared twice after `draw_dir_entry` (double increment). Fixed by using `rax` for strlen result and removing the duplicate `inc r13d`.
  4. **r15 not incremented for dir entries**: Files and dirs overlapped in Y position because `r15d` (visible row counter) was only incremented for file entries. Now incremented for BOTH visible files and visible dirs.
  5. **`mov r14d, [rsp+8]` truncation**: 32-bit load in `draw_file_entry` would truncate addresses above 4GB. Changed to `mov r14, [rsp+8]` (64-bit).
  6. **Click handler counting**: Rewrote `handle_event` to use a single visible row counter (`r15d`) matching the draw order, instead of separate file/dir counters that didn't account for interleaved entries.

- **Build**: `make clean && make -j$(nproc)` succeeds with zero NASM warnings. Scout.exe (6840B code, VirtualSize=8KB) is copied to `\SYSTEM\PROGRAMS\SCOUT.EXE` on disk image.
- **Test**: QEMU boots cleanly — CMD starts, modules load, no crashes.

### Session 10 — XP Start Menu

- **Redesigned Start Menu** (`kernel/gui.c`, `kernel/fb.h`): Single-column dropdown replaced with Windows XP-style two-column layout:
  - **Header**: Blue gradient bar with white "U" user icon and "Default User" label
  - **Left column** (170px): Program shortcuts (Scout, CMD, RENDER) with green icon squares
  - **Right column** (140px): System links (Run..., Help, About BlueOS) on light blue background
  - **Bottom bar**: "Exit BlueOS..." button with hover highlight
  - Separator lines between columns and sections
  - New XP-themed colors in `fb.h` for start menu panes, header, separator, and bottom bar
- **Removed submenu mechanism**: Programs are directly visible in left column (no more "Programs →" submenu to get to Scout/CMD/RENDER)
- **Hover/click logic updated**: `handle_start_menu_click()` and `gui_render()` hover detection rewritten for two-column layout with left-column (programs → pe_spawn), right-column (system actions → text), and bottom bar (Exit → cmd_should_exit)
- **Cleaned up**: Removed `start_submenu_open`, `start_submenu_hovered`, `submenu_items`, `submenu_paths`, `start_num_items`, `start_items[]` — all replaced with new columnar data structures
- **Build**: Compiles cleanly (one warning fixed: unused `total_items` removed)
- **Test**: QEMU boots cleanly

### Session 11 — Radical Start Menu Redesign

- **Fixed hover/click X column detection**: Hover and click handlers now check `mx < sep_x` (left column, programs) vs `mx >= sep_x` (right column, system links). Fixes: hovering "Run..." no longer highlights "Scout", clicking right column items no longer launches programs.
- **Taller gradient header**: `XP_SM_HEADER_H` increased from 40 to 50. Header now draws a blue gradient (interpolated from dark to light blue) via per-row color interpolation. User icon is 32×32 (was 24×24). Two-line text: "Default User" + "Administrator" subtitle in lighter blue.
- **"All Programs" button**: Added as 4th left column item (`start_left_count=4`). Styled differently — no green icon, just text + right-arrow character. Click does nothing (NULL path). Separator above it.
- **Right column expansion**: Changed to "My Documents", "My Computer", "Help & Support", "Run..." (4 items, `start_right_count=4`). Section separator drawn between system items (index 1) and utility items (index 2).
- **Two-column height unification**: Menu total height now based on `max(start_left_count, start_right_count)` instead of `start_left_count` alone. This ensures both columns have enough room.
- **XP-style bottom buttons**: Log Off (left) and Shut Down (right) now drawn with `draw_win3d_rect()` button borders. Log Off highlights blue, Shut Down highlights red (`COL_XP_SM_SHUTDOWN`). Both are properly clickable with X-aware hit detection.
- **Left column etched separators**: Two separators — one after first program (pinned/recent) and one above "All Programs".
- **Commit discipline**: Commits pushed after changes (sessions must always end with commits).

### Session 12 — Timer as Loadable .sys Module

- **Problem**: Timer was the last major driver still statically linked into kernel.elf.
  `timer_handler_and_schedule()` was called directly from `timer_isr_dispatch()` in
  `kernel/module.c`, with no way to load it from disk. Also had chicken-and-egg issue:
  PIT can't start until processes exist, but modules are loaded before process init.
- **Fix**: Extract all timer/scheduling code into `kernel/timer.c` (statically linked):
  `timer_handler_and_schedule()`, `timer_get_ticks()`, `timer_sleep()`,
  `timer_scheduler_enable()`, `timer_init()`, `timer_start()`. The kernel's
  `timer_isr_dispatch()` now calls `kernel/timer.c`'s `timer_handler_and_schedule()`
  directly (removed `registered_timer_sched_handler` mechanism).
- **`modules/timer/timer.c`**: Rewritten as thin .sys module — `module_entry()` programs
  PIT via `api->outb()` and calls `api->timer_scheduler_enable()`. No kernel function
  calls needed (all through API).
- **Init order fix**: Moved `load_disk_modules()` to AFTER `process_init()` and
  `scheduler_init()` in `main.c`. Timer.sys safely starts PIT after processes exist.
- **New files**: `kernel/timer.c`, `modules/timer/timer.ld`
- **API change**: Removed `register_timer_sched_handler` from `kernel_api_t`, added
  `timer_scheduler_enable` for timer.sys to activate scheduling.
- **Build**: `make clean && make -j$(nproc)` succeeds, zero new warnings.
- **Test**: QEMU boot shows `[TIMER] PIT at 100 Hz, scheduling enabled` — timer loaded
  from disk, preemptive multitasking active. No exceptions.
- **Tag**: `v0.8.1` — "Timer as loadable .sys module from disk"
- **Module loading complete**: All 4 loadable modules (DEMO.SYS, KEYB.SYS, MOUSE.SYS,
  TIMER.SYS) now loaded from `/SYSTEM/DRIVERS/` at boot.

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
