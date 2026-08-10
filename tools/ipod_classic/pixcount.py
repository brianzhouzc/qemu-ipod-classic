#!/usr/bin/env python3
"""pixcount.py -- how much of a QEMU screendump is not black.

    (qemu) screendump /path/shot.ppm
    python3 pixcount.py /path/shot.ppm

Prints width, height, the number of non-black pixels, and the eight most common
colours. A black screen and a screen with a picture on it are told apart by the
first number and nothing else, which is the whole point.
"""
import collections
import sys


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # P6 <w> <h> <maxval> then binary RGB. Header fields are whitespace
    # separated and may have # comments.
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
    magic, w, h = fields[0], int(fields[1]), int(fields[2])
    if magic != b'P6':
        raise SystemExit('not a binary PPM: %r' % magic)
    return w, h, data[i:i + w * h * 3]


def main():
    w, h, px = read_ppm(sys.argv[1])
    hist = collections.Counter()
    nonblack = 0
    for k in range(0, len(px), 3):
        c = (px[k], px[k + 1], px[k + 2])
        hist[c] += 1
        if c != (0, 0, 0):
            nonblack += 1
    print('%dx%d = %d pixels' % (w, h, w * h))
    print('non-black: %d (%.1f%%)' % (nonblack, 100.0 * nonblack / (w * h)))
    print('distinct colours: %d' % len(hist))
    for c, n in hist.most_common(8):
        print('   #%02x%02x%02x  %d' % (c[0], c[1], c[2], n))


if __name__ == '__main__':
    main()
