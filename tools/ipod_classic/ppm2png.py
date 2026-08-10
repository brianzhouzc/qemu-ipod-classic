#!/usr/bin/env python3
"""ppm2png.py -- turn a QEMU screendump into a PNG so you can LOOK at it.

    (qemu) screendump "/tmp/shot.ppm"
    python3 ppm2png.py /tmp/shot.ppm /tmp/shot.png

pixcount.py answers "is anything on screen"; this answers "is it the right
thing". Both are needed: on the 5G a screen full of garbage and a screen with
the user interface on it gave nearly the same pixel count, which is how a
broken frame passed for a working one for a while.

Pure standard library on purpose -- no Pillow, no numpy. This WSL has neither
and the whole point is that the check is always available.

It also reads raw framebuffers straight out of guest memory, which is how the
iPod classic's composed frame was identified before anything could draw it:

    (qemu) pmemsave 0x09767a24 0x4b000 "/tmp/layer0.bin"
    python3 ppm2png.py /tmp/layer0.bin /tmp/layer0.png --raw 320x240:argb8888

Formats: argb8888 (bytes B,G,R,A in memory), abgr8888, rgb565, bgr565.
"""
import struct
import sys
import zlib


def read_ppm(path):
    """P6 binary PPM -> (w, h, rgb bytes). Handles # comments in the header."""
    data = open(path, 'rb').read()
    fields = []
    i = 0
    while len(fields) < 4:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b'#':
            while i < len(data) and data[i] != 0x0a:
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    i += 1
    if fields[0] != b'P6':
        raise SystemExit('not a binary PPM: %r' % fields[0])
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[i:i + w * h * 3]


def read_raw(path, spec):
    """"320x240:argb8888" -> (w, h, rgb bytes)."""
    dims, _, fmt = spec.partition(':')
    w, h = (int(v) for v in dims.lower().split('x'))
    d = open(path, 'rb').read()
    out = bytearray()
    if fmt in ('argb8888', 'abgr8888'):
        need = w * h * 4
        if len(d) < need:
            raise SystemExit('%s holds %d bytes, %dx%d at 32bpp needs %d'
                             % (path, len(d), w, h, need))
        for k in range(0, need, 4):
            b0, b1, b2 = d[k], d[k + 1], d[k + 2]
            out += bytes((b2, b1, b0)) if fmt == 'argb8888' else bytes((b0, b1, b2))
    elif fmt in ('rgb565', 'bgr565'):
        need = w * h * 2
        if len(d) < need:
            raise SystemExit('%s holds %d bytes, %dx%d at 16bpp needs %d'
                             % (path, len(d), w, h, need))
        for k in range(0, need, 2):
            v = d[k] | (d[k + 1] << 8)
            c = ((v >> 11) & 0x1f, (v >> 5) & 0x3f, v & 0x1f)
            r, g, b = c[0] * 255 // 31, c[1] * 255 // 63, c[2] * 255 // 31
            out += bytes((r, g, b)) if fmt == 'rgb565' else bytes((b, g, r))
    else:
        raise SystemExit('unknown raw format %r' % fmt)
    return w, h, bytes(out)


def write_png(path, w, h, rgb):
    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack('>I', len(payload)) + body
                + struct.pack('>I', zlib.crc32(body) & 0xffffffff))

    # Filter type 0 on every scanline; the images are small and this keeps the
    # writer to ten lines.
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)


def main():
    args = sys.argv[1:]
    raw = None
    if '--raw' in args:
        i = args.index('--raw')
        raw = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        raise SystemExit(__doc__)
    src, dst = args
    w, h, rgb = read_raw(src, raw) if raw else read_ppm(src)
    write_png(dst, w, h, rgb)
    print('%s  %dx%d' % (dst, w, h))


if __name__ == '__main__':
    main()
