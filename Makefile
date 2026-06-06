CC = gcc
ASM = nasm
LD = ld
GRUB_MKRESCUE = grub-mkrescue
XORRISO = xorriso
QEMU = qemu-system-x86_64
QEMU_BASE = -drive file=disk.img,format=raw,if=ide -boot order=d -usb -device usb-tablet -nic model=rtl8139

CFLAGS = -m64 -ffreestanding -nostdlib -no-pie -mno-red-zone \
         -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector \
         -fno-pie -fno-pic -O1 -Wall -Wextra -I. -I./kernel

ASMFLAGS = -f elf64

LDFLAGS = -m elf_x86_64 -T kernel/linker.ld -nostdlib

KERNEL_SRCS = \
    kernel/boot.asm \
    kernel/isr_loader.asm \
    kernel/timer_isr.asm \
    kernel/switch.asm \
    kernel/syscall_asm.asm \
    kernel/main.c \
    kernel/screen.c \
    kernel/string.c \
    kernel/printf.c \
    kernel/mem.c \
    kernel/idt.c \
    kernel/paging.c \
    kernel/process.c \
    kernel/scheduler.c \
    kernel/vfs.c \
    kernel/blu.c \
    kernel/syscall.c \
    kernel/serial.c \
    kernel/gdt.c \
    kernel/module.c \
    kernel/elf_loader.c \
    kernel/vga.c \
    kernel/fb.c \
    kernel/gui.c \
    kernel/timer.c \
    kernel/pci.c \
    kernel/rtl8139.c \
    modules/ata/ata.c \
    modules/fat/fat.c

KERNEL_OBJS = $(KERNEL_SRCS:.asm=.o)
KERNEL_OBJS := $(KERNEL_OBJS:.c=.o)

.PHONY: all clean run debug iso test-4k test-1080p test-720p test-144hz test-virtio bootloader

all: blueos.iso disk.img bootloader

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	chmod +x $@

blueos.iso: kernel.elf scripts/grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/
	cp scripts/grub.cfg iso/boot/grub/
	$(GRUB_MKRESCUE) -o $@ iso
	rm -rf iso

programs/cmd/cmd.blu: programs/cmd/cmd.asm scripts/make_blu.py
	nasm -f bin -o programs/cmd/cmd.bin programs/cmd/cmd.asm
	python3 scripts/make_blu.py programs/cmd/cmd.bin programs/cmd/cmd.blu 0x400000 0

programs/scout/scout.blu: programs/scout/scout.asm scripts/make_blu.py
	nasm -f bin -o programs/scout/scout.bin programs/scout/scout.asm
	python3 scripts/make_blu.py programs/scout/scout.bin programs/scout/scout.blu 0x400000 0

programs/gui_render/render.blu: programs/gui_render/gui_render.asm scripts/make_blu.py
	nasm -f bin -o programs/gui_render/render.bin programs/gui_render/gui_render.asm
	python3 scripts/make_blu.py programs/gui_render/render.bin programs/gui_render/render.blu 0x400000 0

programs/idle/idle.blu: programs/idle/idle.asm scripts/make_blu.py
	nasm -f bin -o programs/idle/idle.bin programs/idle/idle.asm
	python3 scripts/make_blu.py programs/idle/idle.bin programs/idle/idle.blu 0x400000 0

programs/taskman/taskman.blu: programs/taskman/taskman.asm scripts/make_blu.py
	nasm -f bin -o programs/taskman/taskman.bin programs/taskman/taskman.asm
	python3 scripts/make_blu.py programs/taskman/taskman.bin programs/taskman/taskman.blu 0x400000 0

programs/edit/edit.blu: programs/edit/edit.asm scripts/make_blu.py
	nasm -f bin -o programs/edit/edit.bin programs/edit/edit.asm
	python3 scripts/make_blu.py programs/edit/edit.bin programs/edit/edit.blu 0x400000 0

modules/demo/demo.sys: modules/demo/demo.c modules/demo/demo.ld
	gcc -m64 -ffreestanding -nostdlib -fPIC -I. -I./kernel -c modules/demo/demo.c -o modules/demo/demo.o
	gcc -m64 -ffreestanding -nostdlib -fPIC -shared -Wl,-T,modules/demo/demo.ld -o $@ modules/demo/demo.o

modules/keyb/keyb.sys: modules/keyb/keyb.c modules/keyb/keyb.ld
	gcc -m64 -ffreestanding -nostdlib -fPIC -I. -I./kernel -c modules/keyb/keyb.c -o modules/keyb/keyb.o
	gcc -m64 -ffreestanding -nostdlib -fPIC -shared -Wl,-T,modules/keyb/keyb.ld -o $@ modules/keyb/keyb.o

modules/mouse/mouse.sys: modules/mouse/mouse.c modules/mouse/mouse.ld
	gcc -m64 -ffreestanding -nostdlib -fPIC -I. -I./kernel -c modules/mouse/mouse.c -o modules/mouse/mouse.o
	gcc -m64 -ffreestanding -nostdlib -fPIC -shared -Wl,-T,modules/mouse/mouse.ld -o $@ modules/mouse/mouse.o

modules/timer/timer.sys: modules/timer/timer.c modules/timer/timer.ld
	gcc -m64 -ffreestanding -nostdlib -fPIC -I. -I./kernel -c modules/timer/timer.c -o modules/timer/timer.o
	gcc -m64 -ffreestanding -nostdlib -fPIC -shared -Wl,-T,modules/timer/timer.ld -o $@ modules/timer/timer.o

bootloader/stage1.bin: bootloader/stage1.asm
	nasm -f bin -o $@ $<

bootloader/stage2.bin: bootloader/stage2.asm
	nasm -f bin -o $@ $<

bootloader: bootloader/stage1.bin bootloader/stage2.bin

disk.img: programs/cmd/cmd.blu programs/scout/scout.blu programs/gui_render/render.blu programs/idle/idle.blu programs/taskman/taskman.blu programs/edit/edit.blu modules/demo/demo.sys modules/keyb/keyb.sys modules/mouse/mouse.sys modules/timer/timer.sys scripts/build_image.sh bootloader
	./scripts/build_image.sh
	python3 scripts/install_bootloader.py disk.img

run: blueos.iso disk.img
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 256M -serial stdio -vga std

debug: blueos.iso disk.img
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 256M -serial stdio -vga std -d cpu_reset

test-4k: blueos.iso disk.img
	@echo "=== BlueOS 4K (3840x2160, 512MB RAM, 64MB vgamem) ==="
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 512M -serial file:/tmp/qemu_test_4k.txt -vga std -global VGA.vgamem_mb=64

test-1080p: blueos.iso disk.img
	@echo "=== BlueOS 1080p (1920x1080, 256MB RAM) ==="
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 256M -serial file:/tmp/qemu_test_1080p.txt -vga std

test-720p: blueos.iso disk.img
	@echo "=== BlueOS 720p (1280x720, 256MB RAM) ==="
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 256M -serial file:/tmp/qemu_test_720p.txt -vga std

test-144hz: blueos.iso disk.img
	@echo "=== BlueOS 144Hz (OpenGL display, high refresh rate) ==="
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 256M -serial file:/tmp/qemu_test_144hz.txt -vga std -display sdl,gl=on

test-virtio: blueos.iso disk.img
	@echo "=== BlueOS VirtIO VGA (virtio-gpu device) ==="
	$(QEMU) -cdrom blueos.iso $(QEMU_BASE) -m 256M -serial file:/tmp/qemu_test_virtio.txt -vga virtio

clean:
	rm -f $(KERNEL_OBJS) kernel.elf blueos.iso disk.img
	rm -f programs/cmd/cmd.bin programs/cmd/cmd.blu programs/cmd/cmd.exe
	rm -f programs/scout/scout.bin programs/scout/scout.blu programs/scout/scout.exe
	rm -f programs/gui_render/render.bin programs/gui_render/render.blu programs/gui_render/render.exe
	rm -f programs/idle/idle.bin programs/idle/idle.blu programs/idle/idle.exe
	rm -f programs/taskman/taskman.bin programs/taskman/taskman.blu programs/taskman/taskman.exe
	rm -f programs/edit/edit.bin programs/edit/edit.blu programs/edit/edit.exe
	rm -f modules/keyb/keyb.o modules/keyb/keyb.sys
	rm -f modules/mouse/mouse.o modules/mouse/mouse.sys
	rm -f modules/timer/timer.o modules/timer/timer.sys modules/ata/ata.o modules/fat/fat.o kernel/elf_loader.o
	rm -f modules/demo/demo.o modules/demo/demo.sys
	rm -f bootloader/stage1.bin bootloader/stage2.bin
	rm -rf iso
