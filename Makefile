CC = gcc
CXX = g++
AS = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
LDFLAGS = -melf_i386 -T linker.ld -nostdlib

<<<<<<< HEAD
OBJS = src/boot.o src/interrupts_asm.o src/interrupts.o src/keyboard.o src/kernel.o src/fourty/block_device.o src/fourty/ffs_core.o src/fourty/ffs.o src/clam.o src/ccl.o src/console.o
=======
OBJS = src/boot.o src/interrupts_asm.o src/interrupts.o src/keyboard.o src/kernel.o src/fourty/block_device.o src/fourty/ffs_core.o src/fourty/ffs.o src/clamshell/clamshell.o src/clamlang/ccl_new/ccl.o src/console.o
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e


all: ZirconiumOS.iso

src/boot.o: src/boot.s
	$(AS) $(CFLAGS) -c $< -o $@

src/kernel.o: src/kernel.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/interrupts_asm.o: src/interrupts.s
	$(AS) $(CFLAGS) -c $< -o $@

src/interrupts.o: src/interrupts.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/keyboard.o: src/keyboard.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/fourty/block_device.o: src/fourty/block_device.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/fourty/ffs.o: src/fourty/ffs.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/fourty/ffs_core.o: src/fourty/ffs_core.c
	$(CC) $(CFLAGS) -c $< -o $@

<<<<<<< HEAD
src/clam.o: src/clam.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/ccl.o: src/ccl.c
=======
src/clamshell/clamshell.o: src/clamshell/clamshell.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

src/clamlang/ccl_new/ccl.o: src/clamlang/ccl_new/ccl.c
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
	$(CC) $(CFLAGS) -c $< -o $@

src/console.o: src/console.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

ZirconiumOS.iso: kernel.elf grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o ZirconiumOS.iso iso

clean:
	rm -rf iso
<<<<<<< HEAD
	rm -f kernel.elf ZirconiumOS.iso $(OBJS)
=======
	rm -f kernel.elf ZirconiumOS.iso $(OBJS)
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
