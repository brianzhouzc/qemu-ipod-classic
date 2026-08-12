#!/usr/bin/env python3
"""iremote.py -- closed-loop remote control of the emulated iPod Classic 6G.

The retailOS clickwheel drops SELECT/MENU presses often (~5/7; wiki/11 s.18.8,
s.19.6), so a fully scripted key sequence rarely lands. This drives QEMU through
its TCP monitor and, for the presses that matter, RE-SENDS the key until the
screen actually changes (measured by a pixel diff of two screendumps). That
turns the flaky press into a reliable one without guessing timings.

Key map (hw/arm/ipod_classic/ipod_classic_wheel.c:319):
    q / e  wheel anticlockwise / clockwise (scroll up / down)
    enter  SELECT (centre)      w  MENU (back)
    a / d  LEFT / RIGHT         s  PLAY/PAUSE

Usage (each call connects, acts, screenshots, prints diff, disconnects):
    iremote.py shot NAME                 screendump -> remote/NAME.{ppm,png}
    iremote.py key K [HOLD]              one sendkey, no verify
    iremote.py press K [HOLD] [-u] [-r N] [-t THRESH] [-s SETTLE] [-w WAKE] [--name NAME]
                                         press; with -u retry until screen
                                         changes by >THRESH px (default 1500),
                                         up to N tries; saves after-shot NAME.
                                         -w touches the wheel before each press
                                         so the retailOS honours SELECT (a cold
                                         press is ignored); WAKE may be a comma
                                         pair like q,e that nets zero mid-list.
    iremote.py wake [KEY] [MINNONBLACK]  press KEY until the panel is lit
    iremote.py seq TOK...                one connection; TOK = NAME (screendump)
                                         | K:HOLD (sendkey) | wait:SECONDS
    iremote.py raw 'MONITOR CMD'         arbitrary monitor command
The monitor is at 127.0.0.1:$IREMOTE_PORT (default 4455); shots go to
$IREMOTE_OUT (default /home/dmontero/6g/remote).
"""
import os
import socket
import sys
import time

PORT = int(os.environ.get('IREMOTE_PORT', '4455'))
OUT = os.environ.get('IREMOTE_OUT', '/home/dmontero/6g/remote')
HOST = '127.0.0.1'


def drain(s, idle=0.6):
    s.setblocking(False)
    buf = b''
    last = time.time()
    while time.time() - last < idle:
        try:
            d = s.recv(4096)
            if d:
                buf += d
                last = time.time()
                if buf.rstrip().endswith(b'(qemu)'):
                    break
        except BlockingIOError:
            time.sleep(0.02)
    return buf


def connect():
    s = socket.create_connection((HOST, PORT), timeout=10)
    drain(s)                    # banner + first prompt
    return s


def cmd(s, c, idle=0.6):
    s.setblocking(True)
    s.sendall((c + '\n').encode())
    return drain(s, idle)


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    fields, i = [], 0
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
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[i:i + w * h * 3]


def nonblack(px):
    n = 0
    for k in range(0, len(px), 3):
        if px[k] or px[k + 1] or px[k + 2]:
            n += 1
    return n


def pixdiff(a, b, region='full'):
    """Count differing pixels. region 'right'/'left' restrict to a screen half:
    on the language screen the right ("Welcome") panel is static while the wheel
    only moves the highlight on the left, so a right-half diff tells a real menu
    transition from a mere scroll -- which a full-frame diff cannot."""
    try:
        wa, _, pa = read_ppm(a)
        _, _, pb = read_ppm(b)
    except Exception:
        return -1
    if len(pa) != len(pb):
        return 10 ** 9
    d = 0
    half = wa // 2
    for k in range(0, len(pa), 3):
        if region != 'full':
            x = (k // 3) % wa
            if region == 'right' and x < half:
                continue
            if region == 'left' and x >= half:
                continue
        if pa[k:k + 3] != pb[k:k + 3]:
            d += 1
    return d


def screendump(s, ppm):
    try:
        os.remove(ppm)
    except OSError:
        pass
    cmd(s, 'screendump ' + ppm)
    for _ in range(60):
        if os.path.exists(ppm) and os.path.getsize(ppm) > 1000:
            time.sleep(0.1)          # let the write finish
            return True
        time.sleep(0.1)
    return False


def to_png(ppm):
    png = ppm[:-4] + '.png'
    os.system('python3 /mnt/c/Desarrollo/qemu-ios/tools/ipod_classic/'
              'ppm2png.py "%s" "%s" >/dev/null 2>&1' % (ppm, png))
    return png


def shot(s, name):
    ppm = os.path.join(OUT, name + '.ppm')
    screendump(s, ppm)
    to_png(ppm)
    _, _, px = read_ppm(ppm)
    print('%s: nonblack=%d' % (name, nonblack(px)))


def main():
    os.makedirs(OUT, exist_ok=True)
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    s = connect()
    op = a[0]

    if op == 'shot':
        shot(s, a[1])

    elif op == 'key':
        hold = a[2] if len(a) > 2 else '120'
        cmd(s, 'sendkey %s %s' % (a[1], hold))
        print('sent %s %s' % (a[1], hold))

    elif op == 'raw':
        out = cmd(s, a[1])
        sys.stdout.write(out.decode('latin1'))

    elif op == 'wake':
        # wake [KEY] [MINNONBLACK] -- press KEY until the panel is lit. The
        # panel sleeps ~15 s after boot and a diff against a black frame reads
        # as a false transition, so steps that pixel-diff need a lit baseline.
        key = a[1] if len(a) > 1 else 'e'
        minnb = int(a[2]) if len(a) > 2 else 5000
        ppm = os.path.join(OUT, '_wake.ppm')
        nb = 0
        for k in range(12):
            cmd(s, 'sendkey %s 120' % key)
            time.sleep(1.5)
            screendump(s, ppm)
            _, _, px = read_ppm(ppm)
            nb = nonblack(px)
            if nb > minnb:
                break
        print('wake: nonblack=%d after %d press(es)' % (nb, k + 1))

    elif op == 'seq':
        # seq TOK...  where TOK is 'NAME' (screendump) or 'KEY:HOLD' (sendkey)
        # or 'wait:SECONDS'. One persistent connection for the whole sequence.
        for tok in a[1:]:
            if ':' in tok:
                k, v = tok.split(':', 1)
                if k == 'wait':
                    time.sleep(float(v))
                else:
                    cmd(s, 'sendkey %s %s' % (k, v))
                    print('key %s %s' % (k, v))
                    time.sleep(0.3)
            else:
                shot(s, tok)

    elif op == 'press':
        key = a[1]
        hold = '250'
        until = False
        retries = 6
        thresh = 1500
        settle = 3.5
        name = 'after'
        wake = None        # scroll key to "touch" the wheel before each SELECT
        wakehold = '120'
        region = 'full'    # 'right'/'left' to ignore the scrolling highlight
        i = 2
        while i < len(a):
            t = a[i]
            if t in ('-u', '--until-change'):
                until = True
            elif t in ('-r', '--retries'):
                i += 1
                retries = int(a[i])
            elif t in ('-t', '--thresh'):
                i += 1
                thresh = int(a[i])
            elif t in ('-s', '--settle'):
                i += 1
                settle = float(a[i])
            elif t == '--name':
                i += 1
                name = a[i]
            elif t in ('-w', '--wake'):
                i += 1
                wake = a[i]
            elif t == '--region':
                i += 1
                region = a[i]
            elif t.isdigit():
                hold = t
            i += 1

        before = os.path.join(OUT, '_before.ppm')
        after = os.path.join(OUT, name + '.ppm')
        screendump(s, before)
        tries = 0
        d = 0
        while True:
            tries += 1
            if wake:                        # touch the wheel so the retailOS
                for wk in wake.split(','):  # honours SELECT; a comma-separated
                    cmd(s, 'sendkey %s %s' % (wk, wakehold))  # pair nets zero
                    time.sleep(0.4)         # (e.g. q,e) to re-touch mid-list
                time.sleep(0.4)
            cmd(s, 'sendkey %s %s' % (key, hold))
            time.sleep(settle)
            screendump(s, after)
            d = pixdiff(before, after, region)
            if not until or d > thresh or tries >= retries:
                break
        to_png(after)
        _, _, px = read_ppm(after)
        print('press %s x%d: diff=%d nonblack=%d -> %s.png'
              % (key, tries, d, nonblack(px), name))

    s.close()


if __name__ == '__main__':
    main()
