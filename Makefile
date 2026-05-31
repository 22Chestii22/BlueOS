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
    kernel/timer.c \
    kernel/keyb.c \
    kernel/paging.c \
    kernel/process.c \
    kernel/scheduler.c \
    kernel/vfs.c \
    kernel/ata.c \
    kernel/fat.c \
    kernel/pe.c \
    kernel/syscall.c \
    kernel/serial.c \
    kernel/gdt.c \
    kernel/cmd.c

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

programs/test.exe: programs/test.asm scripts/make_pe.py
	nasm -f bin -o programs/test.bin programs/test.asm
	python3 scripts/make_pe.py programs/test.bin programs/test.exe

programs/edit/edit.exe: programs/edit/edit.asm scripts/make_pe.py
	nasm -f bin -o programs/edit/edit.bin programs/edit/edit.asm
	python3 scripts/make_pe.py programs/edit/edit.bin programs/edit/edit.exe

disk.img: programs/edit/edit.exe programs/test.exe scripts/build_image.sh
	./scripts/build_image.sh

run: blueos.iso disk.img
	$(QEMU) -cdrom blueos.iso -drive file=disk.img,format=raw,if=ide -m 256M -serial stdio -vga std -boot order=d

debug: blueos.iso disk.img
	$(QEMU) -cdrom blueos.iso -drive file=disk.img,format=raw,if=ide -m 256M -serial stdio -vga std -boot order=d -d cpu_reset

clean:
	rm -f $(KERNEL_OBJS) kernel.elf blueos.iso disk.img
	rm -f programs/test.bin programs/test.exe
	rm -f programs/edit/edit.bin programs/edit/edit.exe
	rm -rf iso
