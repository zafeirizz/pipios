# pipios

A small x86-64 operating system written from scratch in C11 and NASM — no
external kernel, no libc, no third-party runtime. The kernel announces itself on
the serial console as `AuroraOS kernel 0.1`.

## What works today (milestones M1–M6)

| Milestone | Contents |
|-----------|----------|
| **M1** boot + core | Two-stage BIOS bootloader (real mode → protected → long mode), GDT/IDT/PIC/PIT at 100 Hz, 64-bit interrupt stubs, serial console, `kprintf`, panic handler |
| **M2** memory | Physical memory manager built from the bootloader memory map, kernel heap (bump allocator + free list), page/heap self-tests |
| **M3** input | PS/2 keyboard and mouse drivers on IRQ1/IRQ12 |
| **M4** storage | ATA PIO driver (LBA28, drive probe, batched writes with explicit `FLUSH`) and a FAT32 implementation: mount, list, create, write, read, mkdir, unlink |
| **M5** graphics | Double-buffered framebuffer compositor: windows, z-order, focus, titlebars, close boxes, PSF font rendering, arrow mouse cursor |
| **M6** desktop + apps | Desktop shell with taskbar and desktop icons; terminal app (scrollback + built-in commands); file manager browsing the real FAT32 volume |

**Not implemented yet:** network stack (QEMU attaches an RTL8139, but there is no
driver), HTML browser, and the package/porting system.

## Prerequisites

`gcc`, `binutils` (`ld`, `objcopy`), `nasm`, `python3`, `qemu-system-x86_64` with
SeaBIOS firmware, `socat` (monitor injection used by the tests), `dosfstools` +
`mtools` (used when building the disk image), and an 8x16 PSF console font
(the kernel's renderer draws 8-pixel-wide glyphs, so the font must be 8 wide).

`tools/setup-env.sh` installs all of this user-locally into `$HOME/sysroot`
without root, and writes a QEMU wrapper into `tools/bin/`. Source the recorded
environment before building:

```sh
bash tools/setup-env.sh     # once (bash, not ./, so it works on a fresh clone)
. tools/env.sh              # each shell
```

## Build, run, test

If you cloned this repository instead of working from the original tree, the
helper scripts may have lost their executable bits (the initial upload went
through the GitHub API, which does not record file modes). Every documented
command invokes them as `bash tools/...`, so this does not affect setup, build
or test. To run them as `./tools/...` instead, restore the bits once:
`chmod +x tools/*.sh tools/tests/*.sh`.

**No usable font?** `make` searches the usual console-font directories and
validates each candidate's header (many `kbd` fonts are named `*8x16*` but are
actually 16 px wide). To use a specific font, override it:
`make FONT_PSF=/usr/share/consolefonts/Uni2-Fixed16.psf.gz`.

```sh
make                        # builds build/disk.img (bootloader + kernel + FAT32 assets)
make run                    # boot in a QEMU window
AURORA_MODE=vnc make run    # boot with a VNC display on :5
make test                   # full suite: M1..M6 boots, PS/2 input, GUI screendump

# useful extras
AURORA_MODE=headless bash tools/qemu.sh       # serial -> build/serial.log, monitor -> build/mon.sock
bash tools/tests/test_boot.sh AURORA-M4-PASS 90    # wait for one milestone marker
bash tools/tests/screendump.sh AURORA-M6-PASS build/gui.ppm 120
python3 tools/ppm2png.py build/gui.ppm build/gui.png
```

The kernel prints a `AURORA-M<n>-PASS` marker on serial as each milestone's
self-tests complete, which is what the tests wait for. The suite takes a few
minutes because every boot test rebuilds a pristine 64 MiB disk image — the
FAT32 tests write to the volume, so a stale image would change their results.

## Layout

```
boot/       boot.asm (MBR + partition table), stage2.asm (loads the kernel, sets long mode)
kernel/     kernel sources: entry/isr asm, gdt, idt, pic, pit, pmm, heap, kbd, mouse,
            ata, fat32, framebuf (compositor), app_term, app_fm, shell, kmain
kernel/include/  shared headers (aurora.h, gui.h)
tools/      image builder, font converter, QEMU helpers, tests
assets/     files baked into the FAT32 partition at image-build time
```

## Notes on the tricky parts

- **ATA + QEMU `FLUSH`.** The driver never issues a command while `BSY` is set,
  uses a generous per-wait deadline, and flushes only at sync points instead of
  once per sector. Giving up in the middle of a `FLUSH` leaves the device wedged
  (every later command is ignored), which showed up as random filesystem
  corruption under host load.
- **Heap vs. framebuffer.** The 3 MiB offscreen compositor buffer is allocated
  from the physical memory manager, not the kernel heap (whose arena is 1 MiB).
  The heap arena is never returned to the PMM — handing it back let a later
  allocation alias the heap, and painting the wallpaper then overwrote live
  window structs.
- **One QEMU per disk image.** Two guests sharing one raw `disk.img` corrupt it;
  the test scripts take care to run one at a time.
