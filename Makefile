# ===========================================================================
# AuroraOS top-level build
# ===========================================================================
BUILD   := build
CC      := gcc
LD      := ld
NASM    := nasm
OBJCOPY := objcopy
PYTHON  := python3

CFLAGS  := -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin \
           -mno-red-zone -mno-sse -mno-mmx -mgeneral-regs-only \
           -O2 -Wall -Wextra -std=c11 -Ikernel
LDFLAGS := -n -T kernel/linker.ld

KSRC    := $(wildcard kernel/*.c)
KOBJS   := $(patsubst kernel/%.c,$(BUILD)/obj/%.o,$(KSRC))
FONTSRC := $(BUILD)/font_data.c

.PHONY: all clean run test

all: $(BUILD)/disk.img

# ---- boot ----
$(BUILD)/boot.bin: boot/boot.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

# ---- font (generated from a PSF console font) ----
$(FONTSRC): tools/psf2c.py | $(BUILD)
	@test -n "$(FONT_PSF)" || { \
	    echo "ERROR: no 8px-wide PSF console font found."; \
	    echo "       run tools/setup-env.sh, or set FONT_PSF=/path/to/8x16-font.psf.gz"; \
	    exit 1; }
	$(PYTHON) tools/psf2c.py $(FONT_PSF) $@

# ---- kernel ----
$(BUILD)/obj/%.o: kernel/%.c | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/entry.o: kernel/entry.asm | $(BUILD)/obj
	$(NASM) -f elf64 $< -o $@

$(BUILD)/obj/isr.o: kernel/isr.asm | $(BUILD)/obj
	$(NASM) -f elf64 $< -o $@

$(BUILD)/obj/font_data.o: $(FONTSRC) | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/obj/entry.o $(BUILD)/obj/isr.o \
                     $(filter-out $(BUILD)/obj/entry.o $(BUILD)/obj/isr.o,$(KOBJS)) \
                     $(BUILD)/obj/font_data.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/obj/entry.o $(BUILD)/obj/isr.o \
	    $(filter-out $(BUILD)/obj/entry.o $(BUILD)/obj/isr.o,$(KOBJS)) \
	    $(BUILD)/obj/font_data.o

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# ---- disk image ----
$(BUILD)/disk.img: $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
                   tools/mkimage.py $(shell find assets -type f 2>/dev/null)
	$(PYTHON) tools/mkimage.py --boot $(BUILD)/boot.bin --stage2 $(BUILD)/stage2.bin \
	    --kernel $(BUILD)/kernel.bin --assets assets --out $@

$(BUILD) $(BUILD)/obj:
	mkdir -p $@

run: all
	AURORA_MODE=window bash tools/qemu.sh

test: all
	bash tools/tests/run_all.sh

clean:
	rm -rf $(BUILD)

# `bash` (not `./`) so a fresh clone without the executable bit still builds.
FONT_PSF ?= $(shell bash tools/find_font.sh)
export FONT_PSF
