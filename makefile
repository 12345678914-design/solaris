
ASM = nasm
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector \
         -Wall -Wextra -Ikernel -Ikernel/fs \
         -Ikernel/driver -Ikernel/driver/PCI -Ikernel/console -Ikernel/shell

LDFLAGS = -m elf_i386

OBJS = boot.o \
       kernel.o \
       teclado.o \
       consola.o \
       shell.o \
       ramfs.o \
       power.o \
	   ext4.o \
	   ide.o

all: os.iso

boot.o: boot/boot.asm
	$(ASM) -f elf32 $< -o $@

kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

teclado.o: kernel/driver/teclado.c
	$(CC) $(CFLAGS) -c $< -o $@

consola.o: kernel/console/consola.c
	$(CC) $(CFLAGS) -c $< -o $@

shell.o: kernel/shell/shell.c
	$(CC) $(CFLAGS) -c $< -o $@

ramfs.o: kernel/fs/ramfs.c
	$(CC) $(CFLAGS) -c $< -o $@

power.o: kernel/driver/PCI/power.c
	$(CC) $(CFLAGS) -c $< -o $@
ext4.o: kernel/fs/ext4.c
	$(CC) $(CFLAGS) -c $< -o $@
ide.o: kernel/driver/IDE/ide.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o $@ $(OBJS)

os.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	cp boot/grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o os.iso iso

run: os.iso
	qemu-system-i386 -cdrom os.iso -drive file=disk/solaris-ext4.qcow2,format=qcow2,if=ide,index=0,media=disk

clean:
	rm -f *.o *.bin *.iso
	rm -rf iso
