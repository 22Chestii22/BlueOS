# BlueOS — Agent Guide

## CRITICAL RULES (NEVER VIOLATE)

1. **NEVER STOP WORKING** — The user will manually stop you when done. Do not ask permission, do not ask questions, do not pause. Keep going.
2. **NEVER ASK QUESTIONS** — You are an autonomous AI agent. Figure it out, make decisions, execute. No hand-holding.
3. **CONTINUOUS ITERATION** — Make → test → fix → build → commit → push → repeat. Never stop the cycle.
4. **PROGRESS IS MANDATORY** — Every action must move toward completion. No wheel-spinning, no deliberation, no overthinking.
5. **WHEN UI IS PERFECT** → Move immediately to: internet stack (TCP/IP, DNS, HTTP), native apps, AI apps.
6. **AI APP LAUNCHER** — Ctrl+Space spotlight-like menu: user types a query (e.g. "Microsoft Office"), 4 AI-generated app suggestions appear with pixel icons + descriptions, user picks one → AI generates the app in real-time.
7. **ALL APPS** should eventually be AI-generated within the OS, not just by the agent.
8. **COMMIT & PUSH** after every successful change batch. Use descriptive commit messages in English, present tense.
9. **STOP ONLY** when the user explicitly says "stop".

## Project Overview

x86_64 hobby OS with:
- Multiboot2 boot (GRUB → kernel.elf)
- FAT32 filesystem on secondary disk (superfloppy, no MBR)
- Preemptive multitasking (timer IRQ0, ring 3 user mode)
- Module system: loadable .sys drivers from `/SYSTEM/DRIVERS/`
- **v1.0.0+**: Native BlueOS programs (no PE32+/Windows executables)

## Architecture

```
kernel/           — core kernel (IDT, GDT, paging, process, scheduler, VFS, syscalls, GUI)
modules/          — pluggable driver modules (.sys, loaded from disk)
  keyb/             keyboard driver (IRQ 1)
  timer/            PIT + scheduler (IRQ 0)
  ata/              ATA PIO disk driver
  fat/              FAT32 filesystem driver
programs/         — user-mode native BlueOS programs (no extensions, Linux-style)
scripts/          — build tools
```

## Roadmap

1. **Pixel-perfect Windows XP GUI clone** — every pixel matches. Aero Glass removed, classic XP Luna theme.
2. **Network stack** — TCP/IP, DNS, HTTP client from scratch.
3. **Native program format** — replace PE32+ with custom `.blu` format (or ELF). Programs: file manager, text editor, terminal, settings panel.
4. **AI integration** — Ctrl+Space Raycast-like launcher, AI-generated UI components.

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

## Program Loading

User-mode programs are loaded from `/SYSTEM/` (PATH default). Format is either:
- **ELF64** (existing elf_loader.c supports R_X86_64_64, R_X86_64_PC32, R_X86_64_JUMP_SLOT)
- **Custom `.blu` format** (to be designed — flat binary with minimal header)

All programs are at image base 0x400000, user stack at 0x70000000.

## Build

```bash
make clean && make -j$(nproc)
make run   # QEMU (standard)
make run-4k # 3840x2160
make debug # QEMU with cpu_reset logging
```

Outputs: `blueos.iso` (bootable CD), `disk.img` (FAT32 data disk).

## Version History

### v0.x — Windows-themed exploration phase
- PE32+ loader (PE executables: CMD, Scout, TaskMan, RENDER, IDLE, EDIT)
- Aero Glass GUI (Windows 7 style)
- Modular drivers (.sys from disk)
- Preemptive multitasking, ring 3 user mode
- VFS, FAT32 filesystem

### v1.0.0+ — Native BlueOS
- **REMOVED**: PE32+ loader, Windows-style .exe programs
- **NEW**: Native program format (`.blu` or ELF)
- **NEW**: Pixel-perfect Windows XP Luna GUI (every pixel matches)
- **NEW**: Network stack (TCP/IP, DNS, HTTP)
- **NEW**: AI launcher (Ctrl+Space)
- **KEPT**: Module system, FAT32, multitasking, paging

## How NOT to Break Things

1. **Superfloppy constraint**: disk.img must be FAT32 with VBR at LBA 0. NO MBR partition table. Kernel reads LBA 0 directly.
2. **Timer ISR**: IDT entry 32 is hardwired to timer_isr → timer_handler_and_schedule. Do NOT use irq_install_handler(0, ...) for timer.
3. **Ring 0 preemption**: Timer ISR skips scheduling when interrupted in ring 0. Ring 0 code must voluntarily call schedule().
4. **process_exit flow**: Calls schedule() then infinite hlt (fallback).
5. **Module includes**: Module .c files use `-I. -I./kernel` for includes.
6. **disk.img format**: `mformat -i disk.img -F ::` (superfloppy mode).
7. **`[rel X + rax]` addressing**: AVOID in NASM. Split into `lea rdi, [rel X]` + `add rdi, rax`.
8. **PML4 permissions**: User-mode pages MUST have Write bit (0x02) at EVERY paging level.
9. **Timer ISR register order**: ctx[i] = frm[14-i] (pushes rax..r15, context expects r15..rax).
10. **syscall clobbers RCX and R11**: Use r10 for 4th syscall arg.
11. **Page-allocated buffers**: gui_alloc_pages() / backbuffer_alloc_pages() map at fixed VADDRs. Must propagate to all processes via paging_map_all_processes().
12. **No BSS section** in user programs: Use `times N db 0` in .text.

## Files Requiring Careful Edits

| File | Why |
|---|---|
| kernel/main.c | Init order matters. Must NOT break existing sequence. |
| kernel/module.c | kernel_api struct with typed function pointers. |
| kernel/process.c | Core lifecycle. process_wait/process_exit/schedule interaction is subtle. |
| kernel/linker.ld | Section layout for multiboot2/GRUB. |
| scripts/build_image.sh | Superfloppy format. Must NOT create MBR. |
| kernel/idt.c | IDT entry 32 = timer_isr hardcoded. |
| kernel/paging.c | map_page_cr3(), split_huge_page(), paging_create_pml4() deep copy. |
| kernel/gui.c | XP theme, window manager, rendering. |
| kernel/fb.c | Framebuffer, backbuffer, blur (to be removed for XP). |
| kernel/boot.asm | Identity mapping count (cmp ecx, 64). Must cover heap + framebuffer. |
| kernel/syscall_asm.asm | Syscall entry (swapgs, no CR3 switch). |
| kernel/switch.asm | Context switch, GS.base management. |

## XP Pixel-Perfect Checklist

- [ ] Luna theme colors (blue/silver/olive)
- [x] Title bar with gradient + icon + text
- [ ] Start menu: classic two-column (pinned + all programs)
- [x] Taskbar with window buttons + tray + clock
- [x] Buttons: XP-style (raised/sunken 3D)
- [x] Scrollbars: XP-style (3D arrows + thumb)
- [x] Desktop: Bliss wallpaper (gradient sky + hills)
- [x] Window borders: classic XP thin borders with rounded corners
- [x] Remove Aero Glass: blur, transparency, glossy reflections
- [x] Start button: pill-shaped with 4-color Windows flag logo
- [ ] Quick Launch section (deferred — not in default XP Luna)

## Commit & Release Rules

After every successful update:
1. `git add -A`
2. `git commit -m "descriptive message in English, present tense"`
3. `git push`
4. Tag significant milestones: `git tag -a v1.x.x -m "v1.x.x: summary"` + `git push origin v1.x.x`
5. Optional: `gh release create v1.x.x --title "v1.x.x" --notes "<summary>"`
