#!/usr/bin/env python3
"""Convert binary PPM (P6) to PNG without external deps."""
import struct, sys, zlib

def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # parse header: P6 <w> <h> <maxval> <raw>
    parts, i = [], 0
    while len(parts) < 4:
        while i < len(data) and data[i] in b' \t\r\n':
            i += 1
        if i < len(data) and data[i] == ord('#'):
            while i < len(data) and data[i] not in b'\r\n':
                i += 1
            continue
        j = i
        while j < len(data) and data[j] not in b' \t\r\n':
            j += 1
        parts.append(data[i:j])
        i = j
    if parts[0] != b'P6':
        raise SystemExit('not a P6 PPM')
    w, h, maxv = int(parts[1]), int(parts[2]), int(parts[3])
    i += 1  # single whitespace after maxval
    raw = data[i:i + w * h * 3]
    return w, h, raw

def write_png(path, w, h, raw):
    def chunk(typ, payload):
        c = struct.pack('>I', len(payload)) + typ + payload
        return c + struct.pack('>I', zlib.crc32(typ + payload) & 0xffffffff)
    rows = b''
    stride = w * 3
    for y in range(h):
        rows += b'\x00' + raw[y * stride:(y + 1) * stride]
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows, 6))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)

if __name__ == '__main__':
    w, h, raw = read_ppm(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1] + '.png'
    write_png(out, w, h, raw)
    print(f'{out}: {w}x{h}')
