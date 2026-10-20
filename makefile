ASM = nasm 
CC = gcc 
LD = ld 
 
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector 
LDFLAGS = -m elf_i386 
 
all: os.iso 
 
boot.o: 
	$(ASM) -f elf32 boot/boot.asm -o boot.o 
 
kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@
ramfs.o: kernel/fs/ramfs.c
	$(CC) $(CFLAGS) -Ikernel/fs -c $< -o $@
kernel.bin: boot.o kernel.o ramfs.o
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o kernel.bin boot.o kernel.o ramfs.o
os.iso: kernel.bin 
	mkdir -p iso/boot/grub 
	cp kernel.bin iso/boot/kernel.bin 
	cp boot/grub.cfg iso/boot/grub/grub.cfg 
	grub-mkrescue -o os.iso iso 
run: os.iso 
	qemu-system-i386 -cdrom os.iso 
clean: 
	rm -rf *.o *.bin *.iso iso/boot 