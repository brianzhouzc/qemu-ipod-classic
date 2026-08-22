# Continuar el trabajo en otra PC

Guía para retomar el emulador de iPod Classic 6G (con el reproductor Now Playing
HLE) en una máquina nueva. Estado del trabajo: `wiki/19-classic6g-dsp-hle.md`
sección **§11.23 "DÓNDE ESTAMOS"**.

## 1. Clonar

```bash
git clone https://github.com/SoftwareEscarlata/qemu-ios.git
cd qemu-ios
git checkout dsp-deadlock-break   # la rama con todo el trabajo
```

## 2. Entorno (WSL Ubuntu 24.04 recomendado, o Linux nativo)

Dependencias de build y de las herramientas de sesión:

```bash
sudo apt update
sudo apt install -y build-essential ninja-build meson pkg-config python3 \
  libglib2.0-dev libpixman-1-dev libsdl2-dev libpulse-dev zlib1g-dev \
  ffmpeg gdb-multiarch
# ffmpeg y ffprobe son necesarios para el HLE de audio (decode host-side + tags).
# gdb-multiarch para las sondas de tools/session-dbg/.
```

## 3. Compilar

```bash
./configure --target-list=arm-softmmu --enable-sdl --enable-plugins   # o el meson setup del repo
ninja -C build qemu-system-arm
# el plugin de traza:  ninja -C build contrib/plugins/libpctrace.so
```

## 4. Assets binarios (NO están en git — ver la Release)

Los binarios (firmware de Apple + imágenes de disco) son grandes y con derechos,
así que van en una **GitHub Release** del repo, no en el árbol git
(`/assets/` está en `.gitignore`). Descárgalos de la Release y colócalos:

| Fichero | Colócalo en | Para qué |
|---|---|---|
| `osos_decrypted.bin` | `assets/classic6g/` | kernel del retailOS (`-kernel`) |
| `rsrc_fat16.img`, `aupd_decrypted.bin`, `bootflash_nor_1MB.bin` | `assets/classic6g/` | recursos / boot del 6G |
| `Emulti.img` | `~/6g/` (o donde prefieras) | disco con música (una canción; para probar el reproductor) |
| `osos.dis`, `iram.dis` | `~/6g/` | desensamblado (VA = off + 0x07FF51B8) que usan las sondas de `tools/session-dbg/` |

Ajusta las rutas `HLE_AUDIO_DISK` / `-drive` a donde pongas `Emulti.img`.

## 5. Lanzar el reproductor Now Playing (HLE)

```bash
export DISPLAY=:0                       # WSLg / X para la ventana
HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=$HOME/6g/Emulti.img HLE_AUDIO_SUSTAIN=1 \
AUDIOPACE=1 AUDIOLIVE=1 HLE_NP_OVERLAY=1 \
  build/qemu-system-arm -M iPod-Classic \
  -kernel assets/classic6g/osos_decrypted.bin \
  -drive if=ide,format=raw,file=$HOME/6g/Emulti.img -snapshot \
  -audio pa -monitor telnet:127.0.0.1:4455,server,nowait -L build/pc-bios
```

Navega con la rueda a **Music → una canción → SELECT**. Aparece el reproductor.
Controles (teclado): `s`=play/pausa, `d`/`a`=siguiente/anterior, `]`/`[`=seek ±5s,
`w`=ocultar/`Enter`=mostrar.

## 6. Herramientas de sesión

En `tools/session-dbg/` están todos los scripts de investigación de esta sesión
(runners `run_*.sh`, sondas gdb `*.gdb` / `*.py`, capturas). Referencian rutas
absolutas de WSL (`/mnt/c/Desarrollo/qemu-ios`, `~/6g/...`) — ajústalas a la ruta
de la nueva máquina antes de usarlas. Control remoto del guest:
`tools/ipod_classic/iremote.py` (rueda vía monitor telnet).

## 7. Qué NO se subió (y por qué)

- Las ~15 GB de variantes de imagen de disco de `~/6g/` (experimentos): solo se
  sube `Emulti.img` (la funcional). El resto se regenera o se pide aparte.
- El resto de `assets/` (rockbox 615M, discos del 5G, ipsw, ref…): no necesarios
  para el reproductor del 6G. Si los necesitas, pídelos.

## Estado actual (resumen)

- **Logrado:** reproductor Now Playing **HLE** usable — audio REAL de la canción
  del disco + pantalla dibujada por QEMU (overlay) + controles play/pausa/seek/
  skip que manejan el audio. Ver `wiki/19` §11.23.
- **No logrado (nativo):** la pantalla Now Playing ORIGINAL de Apple sigue
  bloqueada por el deadlock estructural de MeCCA (§11.12-11.22). El arreglo del
  redirect mantiene vivo al guest, pero su render nativo no produce.
