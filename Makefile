# ---------------------------------------------------------------------------
# n64forthos:  MIPS source -> 64 MiB .z64 cartridge image
#
#   make          build/n64forthos.z64
#   make run      boot it in tools/n64emu.py, save captures/boot.png
#   make gui      boot it in mupen64plus, in a window
#   make test     build the test cartridge, boot it, check the results
#   make clean
#
# The toolchain is Homebrew's LLVM, which targets big-endian MIPS out of the
# box, plus lld -- no cross-gcc to build.  The cartridge is plain mask ROM:
# no save chip, no expansion pak requirement, no coprocessor.
# ---------------------------------------------------------------------------

LLVM    ?= /opt/homebrew/opt/llvm/bin
CC      := $(LLVM)/clang
LD      := /opt/homebrew/opt/lld/bin/ld.lld
OBJCOPY := $(LLVM)/llvm-objcopy

ROM      := build/n64forthos.z64
TESTROM  := build/n64forthos-test.z64

CFLAGS  := -target mips-unknown-elf -march=mips2 -mabi=32 -mno-abicalls \
           -fno-pic -mno-gpopt -G0 -msoft-float -mno-check-zero-division \
           -ffreestanding -fno-builtin -nostdlib -fno-stack-protector \
           -Wall -Wextra -O2 -fomit-frame-pointer
# EXTRA ?= extra flags, e.g. make EXTRA=-DNO_NATIVE to compare against
# the plain interpreter.
EXTRA   ?=
CFLAGS  += $(EXTRA)

LDFLAGS := -T link.ld --no-warnings

CSRC    := src/kernel.c src/video.c src/console.c src/gfx.c src/rdp.c \
           src/input.c \
           src/repl.c src/desktop.c src/forth.c src/native.c
OBJS    := build/entry.o $(patsubst src/%.c,build/%.o,$(CSRC))
TOBJS   := build/entry-t.o $(patsubst src/%.c,build/%-t.o,$(CSRC))
DOBJS   := build/entry-d.o $(patsubst src/%.c,build/%-d.o,$(CSRC))
APP     ?= mandel_fth
APPRUN  ?= MANDEL
APPSIZE ?= 256
GEN     := src/font.h src/system_fth.h src/tests_fth.h \
           src/apps/mandel_fth.h src/apps/cornell_fth.h \
           src/apps/navier_fth.h src/apps/life_fth.h \
           src/apps/wm_fth.h

all: $(ROM)

src/font.h: tools/mkfont.py
	python3 tools/mkfont.py

src/system_fth.h: src/system.fth tools/mkboot.py
	python3 tools/mkboot.py src/system.fth src/system_fth.h system_fth

src/tests_fth.h: test/tests.fth tools/mkboot.py
	python3 tools/mkboot.py test/tests.fth src/tests_fth.h tests_fth

src/apps/%_fth.h: src/apps/%.fth tools/mkboot.py
	python3 tools/mkboot.py --raw $< $@ $*_fth

build/%.o: src/%.c src/n64.h $(GEN) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%-t.o: src/%.c src/n64.h $(GEN) | build
	$(CC) $(CFLAGS) -DTEST_BUILD -c $< -o $@

build/%.o: src/%.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%-t.o: src/%.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%-d.o: src/%.c src/n64.h $(GEN) | build
	$(CC) $(CFLAGS) -DAPP_DEBUG -DAPP_DEBUG_SRC=$(APP) \
	      -DAPP_DEBUG_RUN='"$(APPRUN)"' -DAPP_DEBUG_SIZE=$(APPSIZE) \
	      -c $< -o $@

build/%-d.o: src/%.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel-dbg.elf: $(DOBJS) link.ld
	$(LD) $(LDFLAGS) -o $@ $(DOBJS)

build/n64forthos-dbg.z64: build/ipl3.bin build/kernel-dbg.bin tools/mkrom.py
	python3 tools/mkrom.py build/ipl3.bin build/kernel-dbg.bin $@ N64FORTHOS-D

dbg: build/n64forthos-dbg.z64
	python3 tools/run.py build/n64forthos-dbg.z64 --instr $(INSTR) --text \
	    --shot captures/dbg.png

build/kernel.elf: $(OBJS) link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

build/kernel-test.elf: $(TOBJS) link.ld
	$(LD) $(LDFLAGS) -o $@ $(TOBJS)

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@

build/ipl3.elf: build/ipl3.o
	$(LD) --no-warnings -e ipl3 --section-start=.ipl3=0xA4000040 -o $@ $<

build/ipl3.bin: build/ipl3.elf
	$(OBJCOPY) -O binary --only-section=.ipl3 $< $@

# IPL3 ?= a 0xFB8-byte boot block to use instead of ours.  Our own gets the
# kernel into RDRAM and runs everywhere an emulator is involved, but a real
# console needs one that also initialises RDRAM and satisfies the CIC -- see
# "Real hardware" in the README.
IPL3    ?= build/ipl3.bin

$(ROM): $(IPL3) build/kernel.bin tools/mkrom.py
	python3 tools/mkrom.py $(IPL3) build/kernel.bin $@ N64FORTHOS

$(TESTROM): $(IPL3) build/kernel-test.bin tools/mkrom.py
	python3 tools/mkrom.py $(IPL3) build/kernel-test.bin $@ N64FORTHOS-T

build:
	@mkdir -p build captures

run: $(ROM)
	python3 tools/run.py $(ROM) --shot captures/boot.png

gui: $(ROM)
	mupen64plus --windowed --resolution 640x480 $(ROM)

serve: $(ROM)
	python3 serve.py 8795

test: $(ROM) $(TESTROM)
	python3 -u tools/check.py

clean:
	rm -rf build $(GEN)

INSTR   ?= 40000000

.PHONY: all run gui serve test dbg clean

.PRECIOUS: src/apps/%_fth.h
