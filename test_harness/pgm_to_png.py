#!/usr/bin/env python3
"""Convert PGM (P5) files to 4x-upscaled PNG. Pure Python, no dependencies."""
import struct, zlib, sys, os, glob

def pgm_to_png(pgm_path, png_path, scale=4):
    with open(pgm_path, 'rb') as f:
        f.readline()  # P5
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        w, h = map(int, line.split())
        f.readline()  # maxval
        data = f.read()
    sw, sh = w * scale, h * scale
    raw = bytearray()
    for y in range(sh):
        raw.append(0)  # PNG filter byte
        for x in range(sw):
            raw.append(data[(y // scale) * w + (x // scale)])
    compressed = zlib.compress(bytes(raw), 9)
    def chunk(ct, cd):
        c = ct + cd
        return struct.pack('>I', len(cd)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    with open(png_path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', sw, sh, 8, 0, 0, 0, 0)))
        f.write(chunk(b'IDAT', compressed))
        f.write(chunk(b'IEND', b''))

if __name__ == '__main__':
    build_dir = sys.argv[1] if len(sys.argv) > 1 else 'bin'
    pgms = glob.glob(os.path.join(build_dir, '*.pgm'))
    if not pgms:
        sys.exit(0)
    for pgm in pgms:
        png = pgm.rsplit('.', 1)[0] + '.png'
        pgm_to_png(pgm, png)
        os.remove(pgm)
        print(f'  {os.path.basename(pgm)} -> {os.path.basename(png)}')
    # also remove any stale .bmp files
    for bmp in glob.glob(os.path.join(build_dir, '*.bmp')):
        os.remove(bmp)
