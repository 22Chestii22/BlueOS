CC = gcc
ASM = nasm
LD = ld
GRUB_MKRESCUE = grub-mkrescue
XORRISO = xorriso
QEMU = qemu-system-x86_64

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
    kernel/pe.c \
    kernel/syscall.c \
    kernel/serial.c \
    kernel/gdt.c \
    kernel/module.c \
    kernel/elf_loader.c \
    kernel/vga.c \
    kernel/fb.c \
    kernel/gui.c \
    modules/keyb/keyb.c \
    modules/timer/timer.c \
    modules/ata/ata.c \
    modules/fat/fat.c \
    modules/mouse/mouse.c

KERNEL_OBJS = $(KERNEL_SRCS:.asm=.o)
KERNEL_OBJS := $(KERNEL_OBJS:.c=.o)

.PHONY: all clean run debug iso

all: blueos.iso disk.img

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

programs/cmd/cmd.exe: programs/cmd/cmd.asm scripts/make_pe.py
	nasm -f bin -o programs/cmd/cmd.bin programs/cmd/cmd.asm
	python3 scripts/make_pe.py programs/cmd/cmd.bin programs/cmd/cmd.exe

programs/scout/scout.exe: programs/scout/scout.asm scripts/make_pe.py
	nasm -f bin -o programs/scout/scout.bin programs/scout/scout.asm
	python3 scripts/make_pe.py programs/scout/scout.bin programs/scout/scout.exe

programs/gui_render/render.exe: programs/gui_render/gui_render.asm scripts/make_pe.py
	nasm -f bin -o programs/gui_render/render.bin programs/gui_render/gui_render.asm
	python3 scripts/make_pe.py programs/gui_render/render.bin programs/gui_render/render.exe

programs/idle/idle.exe: programs/idle/idle.asm scripts/make_pe.py
	nasm -f bin -o programs/idle/idle.bin programs/idle/idle.asm
	python3 scripts/make_pe.py programs/idle/idle.bin programs/idle/idle.exe

modules/demo/demo.sys: modules/demo/demo.c modules/demo/demo.ld
	gcc -m64 -ffreestanding -nostdlib -fPIC -I. -I./kernel -c modules/demo/demo.c -o modules/demo/demo.o
	gcc -m64 -ffreestanding -nostdlib -fPIC -shared -Wl,-T,modules/demo/demo.ld -o $@ modules/demo/demo.o

disk.img: programs/cmd/cmd.exe programs/scout/scout.exe programs/gui_render/render.exe programs/idle/idle.exe modules/demo/demo.sys scripts/build_image.sh
	./scripts/build_image.sh

run: blueos.iso disk.img
	$(QEMU) -cdrom blueos.iso -drive file=disk.img,format=raw,if=ide -m 256M -serial stdio -vga std -boot order=d -usb -device usb-tablet

debug: blueos.iso disk.img
	$(QEMU) -cdrom blueos.iso -drive file=disk.img,format=raw,if=ide -m 256M -serial stdio -vga std -boot order=d -usb -device usb-tablet -d cpu_reset

clean:
	rm -f $(KERNEL_OBJS) kernel.elf blueos.iso disk.img
	rm -f programs/cmd/cmd.bin programs/cmd/cmd.exe
	rm -f programs/scout/scout.bin programs/scout/scout.exe
	rm -f programs/gui_render/render.bin programs/gui_render/render.exe
	rm -f programs/idle/idle.bin programs/idle/idle.exe
	rm -f modules/keyb/keyb.o modules/timer/timer.o modules/ata/ata.o modules/fat/fat.o kernel/elf_loader.o
	rm -f modules/demo/demo.o modules/demo/demo.sys
	rm -rf iso
