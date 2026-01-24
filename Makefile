CC  = gcc
CXX = g++
AS  = gcc
LD  = $(CC)
# -ffreestanding
CFLAGS   = -m32 -O2 -Wall -Wextra
CXXFLAGS = -std=c++11 $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS = -m32 -T linker.ld -nostdlib

OBJS = src/boot.o src/interrupts_asm.o src/interrupts.o src/keyboard.o src/kernel.o src/fourty/block_device.o src/fourty/ffs_core.o src/fourty/ffs.o src/clamshell/clamshell.o src/clamlang/compiler/clamlang.o src/clamlang/compiler/lexer.o src/clamlang/compiler/parser.o src/clamlang/compiler/emit.o src/clamlang/runtime/vm/vm.o src/clamlang/runtime/runtime.o src/clamlang/runtime/runner.o src/console.o

all: ZirconiumOS.iso

src/boot.o: src/boot.s
	$(AS) $(CFLAGS) -c $< -o $@

src/kernel.o: src/kernel.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/interrupts_asm.o: src/interrupts.s
	$(AS) $(CFLAGS) -c $< -o $@

src/interrupts.o: src/interrupts.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/keyboard.o: src/keyboard.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/fourty/block_device.o: src/fourty/block_device.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/fourty/ffs.o: src/fourty/ffs.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/fourty/ffs_core.o: src/fourty/ffs_core.c
	$(CC) $(CFLAGS) -c $< -o $@

src/clamshell/clamshell.o: src/clamshell/clamshell.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/clamlang/compiler/clamlang.o: src/clamlang/compiler/clamlang.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/clamlang/compiler/lexer.o: src/clamlang/compiler/lexer.c
	$(CC) $(CFLAGS) -c $< -o $@

src/clamlang/compiler/parser.o: src/clamlang/compiler/parser.c
	$(CC) $(CFLAGS) -c $< -o $@

src/clamlang/compiler/emit.o: src/clamlang/compiler/emit.c
	$(CC) $(CFLAGS) -c $< -o $@

src/clamlang/runtime/vm/vm.o: src/clamlang/runtime/vm/vm.c
	$(CC) $(CFLAGS) -c $< -o $@

src/clamlang/runtime/runtime.o: src/clamlang/runtime/runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

src/clamlang/runtime/runner.o: src/clamlang/runtime/runner.c
	$(CC) $(CFLAGS) -c $< -o $@

src/console.o: src/console.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
