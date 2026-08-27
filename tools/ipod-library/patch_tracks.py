#!/usr/bin/env python3
"""
Put real music on the emulated iPod WITHOUT rewriting its iTunesDB.

Why not just use libgpod: measured, a libgpod rewrite makes retailOS report an
empty library ("Keine Musik") even when the resulting database parses fine and
carries a valid-looking hash58. The shipped database has EIGHT mhsd sections --
types 1, 3, 2, 4, 8, 6, 10 and 5 -- and libgpod 0.8.3 does not round-trip the
later ones. Zeroing the signature does not help either, so it is the structure
being lost, not the hash.

So this leaves the structure alone. The database already describes four tracks
that retailOS reads happily (it shows "4 skladeb" and starts playback), each
pointing at a real file on the FAT. This script:

  - overwrites those files with the songs you want, at the same iPod paths,
  - patches each track record's numeric fields in place (size, duration,
    bitrate, sample rate),
  - replaces only the title/artist/album strings, fixing up the three enclosing
    length fields (mhod -> mhit -> mhsd -> mhbd) that a string change moves.

Every other byte of the database is preserved exactly.

Usage:  patch_tracks.py <mountpoint> < tracks.tsv
        TSV columns: path, title, artist, album, ms, bitrate_kbps, samplerate
"""
import os
import shutil
import struct
import sys

MHOD_TITLE, MHOD_ALBUM, MHOD_ARTIST, MHOD_PATH = 1, 3, 4, 2


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def p32(b, o, v):
    struct.pack_into('<I', b, o, v)


def read_tsv():
    rows = []
    for line in sys.stdin:
        line = line.rstrip('\n')
        if not line:
            continue
        f = line.split('\t')
        if len(f) < 7:
            print('  skipping malformed line: %r' % line[:60], file=sys.stderr)
            continue
        rows.append({
            'path': f[0], 'title': f[1], 'artist': f[2], 'album': f[3],
            'ms': int(f[4]), 'kbps': int(f[5]), 'rate': int(f[6]),
        })
    return rows


def build_mhod(orig, new_text):
    """Rebuild one string mhod with new content, keeping its header bytes."""
    body = new_text.encode('utf-16-le')
    out = bytearray(orig[:40])
    p32(out, 0x08, 40 + len(body))     # total length
    p32(out, 0x1c, len(body))          # string length in bytes
    return bytes(out) + body


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mount = sys.argv[1]
    dbpath = os.path.join(mount, 'iPod_Control', 'iTunes', 'iTunesDB')
    tracks = read_tsv()
    if not tracks:
        print('no tracks on stdin', file=sys.stderr)
        return 1

    d = open(dbpath, 'rb').read()
    assert d[:4] == b'mhbd', 'not an iTunesDB'
    mhbd_hdr = u32(d, 4)

    # Sections, in file order. Only the type-1 one is touched.
    sections, off = [], mhbd_hdr
    while off < len(d) and d[off:off + 4] == b'mhsd':
        total = u32(d, off + 8)
        sections.append((off, total, u32(d, off + 12)))
        off += total
    print('sections: %s' % [(hex(o), t) for o, _, t in sections])

    tsec = [s for s in sections if s[2] == 1]
    if not tsec:
        print('no track section', file=sys.stderr)
        return 1
    soff, stotal, _ = tsec[0]

    mhsd_hdr = u32(d, soff + 4)
    mhlt = soff + mhsd_hdr
    assert d[mhlt:mhlt + 4] == b'mhlt', 'expected mhlt'
    ntracks = u32(d, mhlt + 8)
    mhlt_hdr = u32(d, mhlt + 4)
    print('%d track records in the database' % ntracks)

    # Walk the track records, rebuilding the ones we have songs for.
    new_records = []
    pos = mhlt + mhlt_hdr
    for i in range(ntracks):
        assert d[pos:pos + 4] == b'mhit', 'expected mhit at %#x' % pos
        hdr, total, nmhod = u32(d, pos + 4), u32(d, pos + 8), u32(d, pos + 0x0c)
        rec = bytearray(d[pos:pos + hdr])
        mhods, p = [], pos + hdr
        for _ in range(nmhod):
            mlen = u32(d, p + 8)
            mhods.append(d[p:p + mlen])
            p += mlen

        if i < len(tracks):
            t = tracks[i]
            # The iPod path this record already points at is where the audio
            # must land; keep it and overwrite the file behind it.
            ipod_path = None
            for m in mhods:
                if u32(m, 0x0c) == MHOD_PATH:
                    n = u32(m, 0x1c)
                    ipod_path = m[40:40 + n].decode('utf-16-le')
            if ipod_path is None:
                print('  track %d has no path mhod, skipping' % i)
            else:
                rel = ipod_path.lstrip(':').replace(':', os.sep)
                dst = os.path.join(mount, rel)
                shutil.copyfile(t['path'], dst)
                size = os.path.getsize(dst)
                p32(rec, 0x24, size)             # size in bytes
                p32(rec, 0x28, t['ms'])          # duration, ms
                p32(rec, 0x38, t['kbps'])        # bitrate
                p32(rec, 0x3c, t['rate'] << 16)  # sample rate, 16.16
                rebuilt = []
                for m in mhods:
                    mt = u32(m, 0x0c)
                    if mt == MHOD_TITLE:
                        m = build_mhod(m, t['title'])
                    elif mt == MHOD_ARTIST:
                        m = build_mhod(m, t['artist'])
                    elif mt == MHOD_ALBUM:
                        m = build_mhod(m, t['album'])
                    rebuilt.append(m)
                mhods = rebuilt
                print('  track %d -> %-22.22s %7.1fs %d B  %s'
                      % (i, t['title'], t['ms'] / 1000.0, size, ipod_path))

        body = b''.join(mhods)
        p32(rec, 0x08, hdr + len(body))
        new_records.append(bytes(rec) + body)
        pos += total

    # Everything after the last record inside this section stays as it was.
    tail = d[pos:soff + stotal]

    new_section = bytearray(d[soff:mhlt + mhlt_hdr])
    new_section += b''.join(new_records) + tail
    p32(new_section, 0x08, len(new_section))

    out = bytearray(d[:soff]) + new_section + bytearray(d[soff + stotal:])
    p32(out, 0x08, len(out))                     # mhbd total length

    print('database: %d -> %d bytes' % (len(d), len(out)))
    open(dbpath, 'wb').write(bytes(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
