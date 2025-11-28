CC = gcc
CXX = g++
AS = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
LDFLAGS = -melf_i386 -T linker.ld -nostdlib

OBJS = src/boot.o src/interrupts_asm.o src/interrupts.o src/keyboard.o src/kernel.o src/fourty/block_device.o src/fourty/ffs.o src/clam.o src/console.o


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

src/clam.o: src/clam.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

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
	rm -f kernel.elf ZirconiumOS.iso $(OBJS)
