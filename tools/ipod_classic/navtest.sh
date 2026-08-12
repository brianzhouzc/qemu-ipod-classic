#!/bin/sh
# navtest.sh -- prove that Apple's retailOS on the iPod-Classic machine reacts
# to the click wheel AND to the MENU button, and give tomorrow a way to notice
# when it stops.
#
#   sh tools/ipod_classic/navtest.sh [outdir] [disk-image]
#
# Run it from WSL: the binary in build/ is a Linux ELF.
#
# WHAT IT DOES
#   Boots the retailOS off the firmware partition image, waits for the
#   language screen to settle, takes a baseline, takes a SECOND baseline to
#   show the screen is standing still, and only then starts pressing keys:
#
#     on the LANGUAGE screen   wheel forward twice, wheel back once, SELECT
#     on the MAIN MENU         a second settle check, wheel forward, wheel
#                              back, SELECT into Music, MENU back out, MENU
#                              again at the top of the stack
#
#   The second half is the part that tests going BACK, and it only exists
#   because the first half cannot: on the first-boot language screen Apple
#   binds MENU to an EMPTY rule. Read out of the firmware image at file offset
#   0x0073e628, in the rule list of SettingsMenus_ResetAllSettings_Language_-
#   Layout: trigger "button.menu.up1", handler length 0, action count 0.
#   Nothing to fire. Pressing MENU there is not a test of anything, so this
#   script now walks to a screen that does have somewhere to go back to.
#
# HOW TO READ THE OUTPUT
#   Two numbers per frame: how many pixels are lit, and how many differ from
#   the frame before.
#
#   * The language screen has NO CLOCK, and two frames of a settled one usually
#     differ by exactly 0 pixels. Usually: the battery icon repaints on its own
#     now and then, 23 px at x=145..151, y=4..13 -- one run of this script
#     measured 0 there and the next 23. So the floor is checked against a limit
#     of 200 px, not against zero, and the measured value is printed. Above the
#     limit the whole run is a SUSPENSO, because every threshold below assumes
#     nothing moves that could be mistaken for navigation. 06-menu-idle does the
#     same for the main menu, but only for the LIST half: the preview pane there
#     is not still and should not be, because the label under the icon is longer
#     than the pane in Japanese and scrolls by itself (about 960 px between two
#     shots 8 s apart, all of it in the two lines of text at x=184..293,
#     y=165..182 -- looked at, not just counted).
#   * Moving the selection bar one item has to change about 7 400 pixels: the
#     bar is 160x23 and the row pitch is 24, so two bars' worth change colour.
#     Counted on a real frame, the strict lower bound is 6 584. Anything under
#     ~1 000 is not navigation, whatever else it is.
#   * SELECT leaving a screen changes tens of thousands.
#
#   Two verdicts are not about SIZE at all, and they are the strong ones:
#     - a wheel click on the MAIN MENU has to change the LEFT half (the list)
#       and the RIGHT half (the preview pane) at the same time, because
#       "list.pid.NNN.delayedselected -> navigator.SwitchLayout(
#       MainMenus_Main_Screen_XXX)" says the pane follows the highlight.
#     - MENU out of the Music screen has to land back on the frame we left,
#       so 10-menu-back is compared against 08-menu-wheel-back and has to
#       agree to within a few hundred pixels. "Lots of pixels changed" is not
#       evidence of going back; "changed back into the frame we came from" is.
#
# A KNOWN FLAKE, so that nobody spends an afternoon on it: SELECT is sometimes
# thrown away by the guest. Measured with a dedicated run of five round trips
# (wiki/11 s.18.8): SELECT from the main menu landed 5 times out of 10, MENU 5
# out of 5, with the wheel model reporting every packet queued and none dropped.
# The cause is NOT found; the suspect and the experiment are in that section.
#
# IT IS NOT ONLY 09-into-music, and an earlier version of this header said it
# was. Measured over four runs (wiki/11 s.19.5): SELECT landed 5 times out of
# 7, and one of the two it lost was 05-select, the FIRST one, on the language
# screen. Losing that one is the nastiest case because it does not look like
# itself: every later screenshot lands on the language screen instead of the
# main menu, so 07/08 still show ~8 000 px of list movement -- the language
# list really is moving -- with 0 px in the panel, and that reads like "the
# preview pane is broken" when it is the correct behaviour for the screen we
# are accidentally still on (s.18.5). Before believing any panel or MENU
# failure below, check that 05-select is above 3 000 px.
#
# So: if this run fails at 05-select or ONLY at 09-into-music, repeat it before
# looking for a regression. MENU itself has not been seen to fail since the
# button state started being repeated: 2 of 2 where there was somewhere to go
# back to, and 4 of 4 correctly doing nothing at the top of the stack.
#
# The second signal, and it is independent of the pixels: DISPTRACE=1 prints a
# line every time the guest hands the compositor a new source address, which is
# one repaint. A boot that is standing still emits ten and then nothing. Each
# wheel event the user interface acts on adds at least one. If the pixels move
# but this does not, the screenshot timing is wrong; if this moves but the
# pixels do not, the event arrived and the handler decided the screen did not
# need to change -- or a layer the guest switched off is being drawn over the
# top of everything, which is exactly the bug that used to hide the main menu.

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

# AND NOW LOOK FOR THE ONES THAT PKILL CANNOT SEE, because this has now cost
# four runs across two sessions. The line above only matches the NAME
# "qemu-system-arm"; this tree also produces private copies under other names
# on purpose, so that one session cannot kill another's -- ~/evt/qemu-evt for
# evtrace.py, and ~/6g/ipod-classic-run has been seen too. Those survive, sit
# at 90-plus per cent of a core, and the guest then misses its own boot
# deadlines and software-resets to diskmode: the run below produces a full set
# of screenshots of the wrong screens (wiki/11 s.18.9 and s.19.5).
#
# So do not kill them -- they may be another session's work in flight -- but do
# NOT let this run pretend afterwards that its numbers meant something. Say it
# up front, where it is still cheap to stop.
strays=$(ps -eo comm= 2>/dev/null | grep -c -i -E 'qemu|ipod-classic-run' || true)
[ -n "$strays" ] || strays=0
if [ "$strays" -gt 0 ]; then
    echo "*** ATENCION: hay $strays proceso(s) tipo QEMU corriendo ADEMAS de"
    echo "*** este. 'pkill -x qemu-system-arm' no toca los que se llaman de"
    echo "*** otra forma (qemu-evt, ipod-classic-run). Con la maquina cargada"
    echo "*** el huesped se reinicia a diskmode y el veredicto NO valdra."
    echo "*** Mira 'ps aux | grep -i qemu' y 'uptime' antes de seguir."
    echo
fi

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
    # off an emulated disk, and the transition it plays is a wipe that takes
    # its time. 25 s is past the end of it in every run measured.
    sleep 2;   echo "sendkey ret 150"
    sleep 25;  echo "screendump $out/05-select.ppm"
    # ... and now we are on the main menu, so start again: prove it is still,
    # then move.
    sleep 8;   echo "screendump $out/06-menu-idle.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 8;   echo "screendump $out/07-menu-wheel.ppm"
    sleep 2;   echo "sendkey q 150"
    sleep 8;   echo "screendump $out/08-menu-wheel-back.ppm"
    sleep 2;   echo "sendkey ret 150"
    sleep 12;  echo "screendump $out/09-into-music.ppm"
    sleep 2;   echo "sendkey w 150"
    sleep 12;  echo "screendump $out/10-menu-back.ppm"
    sleep 2;   echo "sendkey w 150"
    sleep 10;  echo "screendump $out/11-menu-at-top.ppm"
    sleep 1;   echo quit
} | DISPTRACE=1 WHEELTRACE=1 "$qemu" -M iPod-Classic \
        -kernel "$osos" \
        -drive "if=ide,format=raw,file=$disk,file.locking=off" \
        -snapshot \
        -display none -serial null -monitor stdio \
        >/dev/null 2>"$out/qemu.err" || true

grep -c SRC_SLOT0 "$out/qemu.err" > "$out/disp.txt" 2>/dev/null || echo 0 > "$out/disp.txt"

echo
# A run in which the guest rebooted has nothing to say: every sleep above is
# counted from power-on, so one reset slides the whole schedule and the
# screenshots land on the wrong screens. Measured example: a run that reset
# once showed the Low Battery screen where 00-idle expects the language list
# and a black panel where 02-wheel-fwd expects a moved highlight. Say so
# loudly instead of letting the verdicts pretend.
# "grep -c" prints 0 AND exits 1 when there is no match, so a "|| echo 0" tail
# makes the variable "0\n0" and the test below dies with "Illegal number".
resets=$(grep -c "software reset" "$out/qemu.err" 2>/dev/null || true)
[ -n "$resets" ] || resets=0
if [ "$resets" -gt 0 ]; then
    echo "*** ATENCION: el huesped se reinicio $resets vez/veces durante la"
    echo "*** carrera (SWRCON = 0xaa5 en stderr). Todos los tiempos de abajo"
    echo "*** se cuentan desde el encendido, asi que las capturas caen en"
    echo "*** pantallas equivocadas y el veredicto NO vale. Repite la carrera."
    echo "*** Antes de buscar culpables: ps aux | grep qemu, y uptime."
    echo
fi
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

def differ(a, b, x0=0, x1=None, w=320):
    x1 = w if x1 is None else x1
    n = 0
    h = len(a) // (w * 3)
    for y in range(h):
        row = y * w
        for x in range(x0, x1):
            i = (row + x) * 3
            if a[i:i + 3] != b[i:i + 3]:
                n += 1
    return n

shots = sorted(glob.glob(os.path.join(out, "*.ppm")))
if not shots:
    print("NO HAY CAPTURAS: la maquina no llego a arrancar")
    raise SystemExit(1)

prev = None
frames = {}
diffs = {}
lefts = {}
lit = {}
for path in shots:
    w, h, data = read_ppm(path)
    write_png(path[:-4] + ".png", w, h, data)
    name = os.path.basename(path)[:-4]
    frames[name] = data
    n = sum(1 for i in range(0, len(data), 3) if data[i:i + 3] != b"\x00\x00\x00")
    lit[name] = n
    if prev is None:
        print("%-20s %6d px encendidos" % (name, n))
    else:
        d = differ(data, prev)
        # Where did it change? The list is the left 160 columns; the preview
        # panel is the right 160. A moved highlight is a LEFT-hand event and a
        # preview that follows it is a RIGHT-hand one.
        left = differ(data, prev, 0, 160)
        diffs[name] = d
        lefts[name] = left
        print("%-20s %6d px encendidos   %6d px distintos (%d en la lista, "
              "%d en el panel)" % (name, n, d, left, d - left))
    prev = data

print()
ok = True

FLOOR_MAX = 200


def floor_check(name, where, half=None):
    """half=None: the whole frame has to be still. half='left': only the list.

    WHY THE LIMIT IS 200 AND NOT 0. These screens have no clock and for most of
    a run they really do stand at 0 px between two shots. But two things move on
    their own and both are correct:

      * THE BATTERY ICON, 23 px at x=145..151, y=4..13, with the green fill
        among the colours -- measured on the language screen and again between
        two visits to the main menu. It repaints when it feels like it: one run
        of this script showed 0 px here and the next showed 23.
      * THE SCROLLING LABEL of the main menu's preview pane, about 960 px, which
        is why the main menu is only floored on its LIST half (see below).

    So the premise is not "nothing moves", it is "nothing moves that could be
    mistaken for navigation". Moving the highlight one row costs 8 000 px,
    forty times this limit, and the smallest verdict below asks for 3 000. The
    measured value is printed every run so that a change is visible even when
    it passes."""
    global ok
    d = diffs.get(name) if half is None else lefts.get(name)
    if d is None:
        print("SUSPENSO: falta %s, no se puede comprobar el suelo de %s"
              % (name, where))
        ok = False
    elif d > FLOOR_MAX:
        # Y esto SUSPENDE la prueba, no es un aviso. Todo el veredicto de abajo
        # descansa en que en estas pantallas no se mueve nada que pueda
        # confundirse con navegar. Si deja de ser cierto, los umbrales dejan de
        # distinguir "se movio el resaltado" de "parpadeo algo", y un test que
        # sigue diciendo OK con su premisa rota es peor que ninguno.
        print("SUSPENSO: %s cambia %d px sin tocar nada, mas de los %d que "
              "cuesta el icono de la bateria. El suelo de ruido de %s ya no "
              "vale y los umbrales de abajo tampoco."
              % (name, d, FLOOR_MAX, where))
        ok = False
    else:
        print("  suelo de %s: %d px (limite %d)" % (where, d, FLOOR_MAX))

floor_check("01-idle-again", "la pantalla de idiomas")
floor_check("06-menu-idle", "la LISTA del menu principal", half="left")
if "06-menu-idle" in diffs:
    print("  (el panel del menu principal NO esta quieto: %d px, y es correcto "
          "-- la etiqueta larga se desplaza sola. Por eso el umbral del panel "
          "es 3000 y no 1.)" % (diffs["06-menu-idle"] - lefts["06-menu-idle"]))

def verdict(name, what, hard=True):
    global ok
    d = diffs.get(name)
    tag = "" if hard else "   [informativo]"
    if d is None:
        print("  %-20s SIN CAPTURA%s" % (name, tag))
        if hard:
            ok = False
    elif d >= 3000:
        print("  %-20s %6d px  -> %s%s" % (name, d, what, tag))
    elif d >= 1000:
        print("  %-20s %6d px  -> algo se movio, pero menos de lo que cuesta "
              "un elemento entero%s" % (name, d, tag))
        if hard:
            ok = False
    else:
        print("  %-20s %6d px  -> NO. Eso no es navegar%s" % (name, d, tag))
        if hard:
            ok = False

def verdict_both_halves(name, hard=True):
    """The preview pane has to move WITH the highlight, not instead of it."""
    global ok
    d, l = diffs.get(name), lefts.get(name)
    if d is None:
        print("  %-20s SIN CAPTURA" % name)
        if hard:
            ok = False
        return
    r = d - l
    if l >= 3000 and r >= 3000:
        print("  %-20s lista %5d px + panel %5d px  -> el resaltado se movio "
              "Y el panel lo siguio" % (name, l, r))
    else:
        print("  %-20s lista %5d px + panel %5d px  -> NO: hacen falta las "
              "dos mitades" % (name, l, r))
        if hard:
            ok = False

def verdict_same_as(name, ref, tol=1500, only_if=None):
    """Did MENU land back on the frame we left, or just somewhere else?

    The tolerance has room in it on purpose: a real change of screen costs
    6 000 px or more here, and the only thing that legitimately differs
    between two visits to the same menu is the scrolling label in the preview
    pane, worth under 1 000. 1 500 separates the two with no overlap. Measured
    on the run that went in: 0 px."""
    global ok
    if name not in frames or ref not in frames:
        print("  %-20s SIN CAPTURA" % name)
        ok = False
        return
    # A trap worth guarding: if SELECT never got us out of the main menu, then
    # "MENU landed back on the frame we left" is TRUE for the stupidest of
    # reasons -- we never left. Without this the test would pass hardest
    # exactly when the navigation did nothing at all.
    if only_if is not None and diffs.get(only_if, 0) < 3000:
        print("  %-20s no se puede decir nada: %s no llego a cambiar de "
              "pantalla, asi que no hubo viaje de vuelta que medir"
              % (name, only_if))
        ok = False
        return
    d = differ(frames[name], frames[ref])
    if d <= tol:
        print("  %-20s %6d px de diferencia con %s  -> MENU VOLVIO A LA MISMA "
              "PANTALLA" % (name, d, ref))
    else:
        print("  %-20s %6d px de diferencia con %s  -> cambio, pero NO es la "
              "pantalla de la que salimos" % (name, d, ref))
        ok = False

print("VEREDICTO")
print(" la rueda en la pantalla de idiomas:")
verdict("02-wheel-fwd",  "el resaltado se movio")
verdict("03-wheel-fwd2", "el resaltado se movio")
verdict("04-wheel-back", "el resaltado volvio")
print()
print(" SELECT, que sale de la pantalla de idiomas al menu principal:")
verdict("05-select", "SELECT cambio la pantalla")
print()
print(" la rueda en el menu principal -- las DOS mitades:")
verdict_both_halves("07-menu-wheel")
verdict_both_halves("08-menu-wheel-back")
print()
print(" entrar y VOLVER:")
verdict("09-into-music", "SELECT entro en Musica")
verdict("10-menu-back",  "MENU cambio la pantalla")
verdict_same_as("10-menu-back", "08-menu-wheel-back", only_if="09-into-music")
print()
print(" MENU en lo alto de la pila -- aqui lo correcto es que NO pase nada:")
print("   en el menu principal no hay pantalla debajo a la que volver, asi que")
print("   la lista tiene que quedarse quieta. El panel puede moverse: su")
print("   etiqueta se desplaza sola.")
l = lefts.get("11-menu-at-top")
if l is None:
    print("  %-20s SIN CAPTURA" % "11-menu-at-top")
    ok = False
elif l <= FLOOR_MAX:
    print("  %-20s lista %5d px + panel %5d px  -> no navego, que es lo que "
          "toca" % ("11-menu-at-top", l, diffs["11-menu-at-top"] - l))
else:
    print("  %-20s lista %5d px  -> la lista se movio, y desde el menu "
          "principal MENU no deberia llevar a ninguna parte"
          % ("11-menu-at-top", l))
    ok = False
print()
print("PNGs en %s" % out)
raise SystemExit(0 if ok else 2)
EOF
