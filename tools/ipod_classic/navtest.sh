#!/bin/sh
# navtest.sh -- prove that Apple's retailOS on the iPod-Classic machine reacts
# to the click wheel, and give tomorrow a way to notice when it stops.
#
#   sh tools/ipod_classic/navtest.sh [outdir] [disk-image]
#
# Run it from WSL: the binary in build/ is a Linux ELF.
#
# WHAT IT DOES
#   Boots the retailOS off the firmware partition image, waits for the
#   language screen to settle, takes a baseline, takes a SECOND baseline to
#   show the screen is standing still, and only then starts pressing keys:
#   wheel forward twice, wheel back once, then SELECT, then MENU.
#
# HOW TO READ THE OUTPUT
#   Two numbers per frame: how many pixels are lit, and how many differ from
#   the frame before.
#
#   * The language screen has NO CLOCK. Two frames of a settled screen differ
#     by exactly 0 pixels -- measured, five shots over 20 s. So on this screen
#     any non-zero difference is real, and the second baseline is there to
#     prove the floor is still 0 before anything is claimed.
#   * Moving the selection bar one item has to change about 7 400 pixels: the
#     bar is 160x23 and the row pitch is 24, so two bars' worth change colour.
#     Counted on a real frame, the strict lower bound is 6 584. Anything under
#     ~1 000 is not navigation, whatever else it is.
#   * SELECT leaving the language screen changes tens of thousands.
#
#   The verdict at the end applies those thresholds. "px distintos" of a few
#   dozen would be a status-bar icon and is deliberately NOT counted as a win.
#
# The second signal, and it is independent of the pixels: DISPTRACE=1 prints a
# line every time the guest hands the compositor a new source address, which is
# one repaint. A boot that is standing still emits ten and then nothing. Each
# wheel event the user interface acts on adds at least one. If the pixels move
# but this does not, the screenshot timing is wrong; if this moves but the
# pixels do not, the event arrived and the handler decided the screen did not
# need to change.

set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
qemu="$root/build/qemu-system-arm"
osos="$root/assets/classic6g/osos_decrypted.bin"
out=${1:-$HOME/6g/nav}
disk=${2:-$HOME/6g/E.img}

[ -x "$qemu" ] || { echo "no encuentro $qemu; compila primero" >&2; exit 1; }
[ -f "$osos" ] || { echo "no encuentro $osos" >&2; exit 1; }
[ -f "$disk" ] || {
    echo "no encuentro el disco $disk." >&2
    echo "Se fabrica asi (wiki/11 sec.15):" >&2
    echo "  truncate -s 64M \$HOME/6g/fat32.img" >&2
    echo "  mkfs.vfat -F 32 -n IPOD \$HOME/6g/fat32.img" >&2
    echo "  python3 tools/ipod_classic/mkdisk6g4k.py \$HOME/6g/E.img \\" >&2
    echo "          assets/extracted/Firmware-24.9.1.2 \$HOME/6g/fat32.img" >&2
    exit 1
}

mkdir -p "$out"
rm -f "$out"/*.ppm "$out"/*.png "$out"/disp.txt "$out"/qemu.err

# -x, matching the process NAME, not -f: a "pkill -f qemu-system-arm" also
# matches any shell whose own command line mentions it, including the one that
# launched this script. That self-kill costs a whole boot to work out.
pkill -9 -x qemu-system-arm 2>/dev/null || true
sleep 1

# The first pixel lands between 22 and 30 s of wall clock and there is one
# late repaint at about 39 s; 48 s is past both with room to spare. Keys are
# held for 150 ms because the wheel model paces its packets on the virtual
# clock and a zero-length press can fall between two of its ticks.
{
    sleep 48;  echo "screendump $out/00-idle.ppm"
    sleep 4;   echo "screendump $out/01-idle-again.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 4;   echo "screendump $out/02-wheel-fwd.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 4;   echo "screendump $out/03-wheel-fwd2.ppm"
    sleep 2;   echo "sendkey q 150"
    sleep 4;   echo "screendump $out/04-wheel-back.ppm"
    # SELECT gets a long leash: leaving this screen means loading another one
    # off an emulated disk, and two shots twenty seconds apart tell a slow
    # transition apart from no transition at all.
    sleep 2;   echo "sendkey ret 150"
    sleep 20;  echo "screendump $out/05-select.ppm"
    sleep 20;  echo "screendump $out/06-select-later.ppm"
    sleep 2;   echo "sendkey w 150"
    sleep 15;  echo "screendump $out/07-menu.ppm"
    sleep 1;   echo quit
} | DISPTRACE=1 WHEELTRACE=1 "$qemu" -M iPod-Classic \
        -kernel "$osos" \
        -drive "if=ide,format=raw,file=$disk,file.locking=off" \
        -snapshot \
        -display none -serial null -monitor stdio \
        >/dev/null 2>"$out/qemu.err" || true

grep -c SRC_SLOT0 "$out/qemu.err" > "$out/disp.txt" 2>/dev/null || echo 0 > "$out/disp.txt"

echo
echo "=== lo que dijo el modelo de la rueda ==="
grep WHEELTRACE "$out/qemu.err" || echo "  (nada: WHEELTRACE no imprimio)"
echo
echo "=== repintados (escrituras de la direccion de origen de la capa 0) ==="
echo "  $(cat "$out/disp.txt")   (una carrera en reposo son 10)"
echo

python3 - "$out" <<'EOF'
import glob, os, struct, sys, zlib

out = sys.argv[1]

def read_ppm(path):
    blob = open(path, "rb").read()
    parts = blob.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

def write_png(path, w, h, data):
    raw = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xffffffff))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    open(path, "wb").write(png)

shots = sorted(glob.glob(os.path.join(out, "*.ppm")))
if not shots:
    print("NO HAY CAPTURAS: la maquina no llego a arrancar")
    raise SystemExit(1)

prev = None
diffs = {}
lit = {}
for path in shots:
    w, h, data = read_ppm(path)
    write_png(path[:-4] + ".png", w, h, data)
    name = os.path.basename(path)[:-4]
    n = sum(1 for i in range(0, len(data), 3) if data[i:i + 3] != b"\x00\x00\x00")
    lit[name] = n
    if prev is None:
        print("%-18s %6d px encendidos" % (name, n))
    else:
        d = sum(1 for i in range(0, len(data), 3) if data[i:i + 3] != prev[i:i + 3])
        # Where did it change? The list is the left 160 columns; the preview
        # panel is the right 160. A moved highlight is a LEFT-hand event.
        left = sum(1 for y in range(h) for x in range(160)
                   if data[(y * w + x) * 3:(y * w + x) * 3 + 3]
                   != prev[(y * w + x) * 3:(y * w + x) * 3 + 3])
        diffs[name] = d
        print("%-18s %6d px encendidos   %6d px distintos (%d en la lista, "
              "%d en el panel)" % (name, n, d, left, d - left))
    prev = data

print()
floor = diffs.get("01-idle-again", None)
if floor is None:
    print("VEREDICTO: faltan capturas, no se puede decir nada")
    raise SystemExit(1)

ok = True
if floor != 0:
    print("AVISO: la pantalla en reposo cambia %d px entre dos capturas. "
          "El suelo de ruido ya no es 0 y los umbrales de abajo no valen."
          % floor)

def verdict(name, what, hard=True):
    global ok
    d = diffs.get(name)
    tag = "" if hard else "   [informativo]"
    if d is None:
        print("  %-17s SIN CAPTURA%s" % (name, tag))
        if hard:
            ok = False
    elif d >= 3000:
        print("  %-17s %6d px  -> %s%s" % (name, d, what, tag))
    elif d >= 1000:
        print("  %-17s %6d px  -> algo se movio, pero menos de lo que cuesta "
              "un elemento entero%s" % (name, d, tag))
        if hard:
            ok = False
    else:
        print("  %-17s %6d px  -> NO. Eso no es navegar%s" % (name, d, tag))
        if hard:
            ok = False

print("VEREDICTO")
print(" la rueda -- esto es lo que decide si el test pasa:")
verdict("02-wheel-fwd",  "el resaltado se movio")
verdict("03-wheel-fwd2", "el resaltado se movio")
verdict("04-wheel-back", "el resaltado volvio")
print()
print(" los botones -- NO deciden todavia, y hay que decir por que:")
print("   SELECT y MENU llegan enteros al huesped. Medido con gdbpoke.py:")
print("   el codigo de tecla remapeado (0x6f para SELECT) atraviesa el")
print("   despachador 0x0803f390 y llega a 0x08170e94, que construye el")
print("   mensaje 'kDwn' y lo mete en la cola del EventManager. Lo que la")
print("   pantalla de idioma hace con el sigue siendo DESCONOCIDO, asi que")
print("   estas dos lineas se miden y se enseñan, no se aprueban.")
verdict("05-select",       "SELECT cambio la pantalla", hard=False)
verdict("06-select-later", "cambio, tarde",             hard=False)
verdict("07-menu",         "MENU cambio la pantalla",   hard=False)
print()
print("PNGs en %s" % out)
raise SystemExit(0 if ok else 2)
EOF
