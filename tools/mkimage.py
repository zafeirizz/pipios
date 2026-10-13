#!/usr/bin/env python3
"""
Build the AuroraOS raw disk image:
  LBA 0        MBR (boot code + partition table)
  LBA 1..120   stage2 (120 sectors reserved)
  LBA 121..    kernel flat binary
  LBA 4096..   FAT32 partition (populated with assets/)
"""
import argparse, os, struct, subprocess, sys, shutil, tempfile

LBA_SIZE = 512
STAGE2_SECTORS = 120      # must match boot/stage2.asm padding
KERNEL_LBA = 1 + STAGE2_SECTORS
PART_START = 4096         # 2 MiB gap for bootloader + kernel
IMG_SIZE = 64 * 1024 * 1024

def die(msg):
    print(f'mkimage: ERROR: {msg}', file=sys.stderr)
    sys.exit(1)

def find_tool(*names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    home = os.environ.get('HOME', '')
    for n in names:
        for sub in ('usr/bin', 'usr/sbin', 'bin', 'sbin'):
            p = os.path.join(home, 'sysroot', sub, n)
            if os.path.exists(p):
                return p
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--boot', required=True)
    ap.add_argument('--stage2', required=True)
    ap.add_argument('--kernel', required=True)
    ap.add_argument('--assets', default='assets')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    boot = bytearray(open(args.boot, 'rb').read())
    stage2 = bytearray(open(args.stage2, 'rb').read())
    kernel = open(args.kernel, 'rb').read()

    if len(boot) != 512:
        die(f'boot.bin is {len(boot)} bytes, expected 512')
    if len(stage2) > STAGE2_SECTORS * LBA_SIZE:
        die(f'stage2 too big: {len(stage2)} > {STAGE2_SECTORS*512}')
    max_kernel = (PART_START - KERNEL_LBA) * LBA_SIZE
    if len(kernel) > max_kernel:
        die(f'kernel too big: {len(kernel)} > {max_kernel} '
            f'(raise PART_START in tools/mkimage.py)')

    # -- patch boot sector: stage2 sector count at header (magic MBH @ 3) --
    i = boot.find(b'MBH')
    if i < 0:
        die('MBH magic not found in boot.bin')
    stage2_sect = (len(stage2) + LBA_SIZE - 1) // LBA_SIZE
    struct.pack_into('<I', boot, i + 3, stage2_sect)

    # -- patch stage2 header (magic S2H @ 3): kernel_lba, kernel_sectors --
    j = stage2.find(b'S2H')
    if j < 0:
        die('S2H magic not found in stage2.bin')
    kern_sect = (len(kernel) + LBA_SIZE - 1) // LBA_SIZE
    struct.pack_into('<I', stage2, j + 3, KERNEL_LBA)
    struct.pack_into('<I', stage2, j + 7, kern_sect)

    # -- partition table entry (type 0x0C FAT32 LBA) --
    total_lbas = IMG_SIZE // LBA_SIZE
    entry = struct.pack('<BBBBBBBBII',
                        0x80, 0xFE, 0xFF, 0xFF,      # status + bogus CHS
                        0x0C,                        # type: FAT32 LBA
                        0xFE, 0xFF, 0xFF,            # end CHS
                        PART_START, total_lbas - PART_START)
    boot[0x1BE:0x1BE + 16] = entry
    if boot[510] != 0x55 or boot[511] != 0xAA:
        die('missing 0x55AA signature')

    # -- write raw image --
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'wb') as f:
        f.write(boot)
        f.write(stage2)
        f.write(b'\x00' * (KERNEL_LBA * LBA_SIZE - len(boot) - len(stage2)))
        f.write(kernel)
        f.write(b'\x00' * (PART_START * LBA_SIZE - KERNEL_LBA * LBA_SIZE - len(kernel)))
        f.write(b'\x00' * (IMG_SIZE - PART_START * LBA_SIZE))

    # -- format a standalone FAT32 partition, then splice it into the image --
    mkfs = find_tool('mkfs.fat', 'mkfs.vfat')
    if not mkfs:
        die('mkfs.fat not found (run tools/setup-env.sh)')
    off_bytes = PART_START * LBA_SIZE
    part_blocks = (IMG_SIZE // LBA_SIZE) - PART_START
    part_file = args.out + '.part'
    with open(part_file, 'wb') as f:
        f.write(b'\x00' * (part_blocks * LBA_SIZE))
    r = subprocess.run([mkfs, '-F', '32', '-n', 'AURORAOS', part_file],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die(f'mkfs.fat failed:\n{r.stdout}\n{r.stderr}')
    # patch hidden-sectors BPB field so it matches the partition start
    with open(part_file, 'r+b') as f:
        f.seek(0x1C)
        f.write(struct.pack('<I', PART_START))
    with open(part_file, 'rb') as pf, open(args.out, 'r+b') as img:
        img.seek(off_bytes)
        shutil.copyfileobj(pf, img, 1024 * 1024)
    os.unlink(part_file)

    # -- populate with mtools (drive z: configured via MTOOLSRC) --
    mcopy = find_tool('mcopy')
    mmd = find_tool('mmd')
    if not mcopy or not mmd:
        die('mtools not found (run tools/setup-env.sh)')
    conf = tempfile.NamedTemporaryFile('w', suffix='.conf', delete=False)
    conf.write(f'drive z: file="{os.path.abspath(args.out)}" offset={off_bytes}\n')
    conf.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK='1', MTOOLSRC=conf.name)

    def sh(cmd, fatal=True):
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if fatal and r.returncode != 0:
            die(f'{" ".join(cmd)} failed:\n{r.stdout}\n{r.stderr}')
        return r

    def mkdir_p(path):
        parts = [p for p in path.strip('/').split('/') if p]
        cur = ''
        for p in parts:
            cur += '/' + p
            sh([mmd, f'z:{cur}'], fatal=False)

    assets = args.assets
    if os.path.isdir(assets):
        mkdir_p('/')
        for root, dirs, files in os.walk(assets):
            dirs.sort()
            rel = os.path.relpath(root, assets)
            if rel != '.':
                mkdir_p('/' + rel.replace(os.sep, '/'))
            for fn in sorted(files):
                src = os.path.join(root, fn)
                tgt = '/' + (rel.replace(os.sep, '/') + '/' + fn if rel != '.' else fn)
                sh([mcopy, src, 'z:' + tgt.replace('//', '/')])
    os.unlink(conf.name)

    print(f'mkimage: {args.out} ({IMG_SIZE//1024//1024} MiB), '
          f'stage2 {stage2_sect} sectors, kernel {len(kernel)/1024:.1f} KiB '
          f'@ LBA {KERNEL_LBA}, FAT32 @ LBA {PART_START}')

if __name__ == '__main__':
    main()
