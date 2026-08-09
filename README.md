# Emulación de un iPod Video 5G en QEMU

Este fork de [QEMU-iOS](https://github.com/devos50/qemu-ios) añade una máquina
**`iPod-Video`** que emula un iPod Video de 5ª generación (SoC PortalPlayer
PP5022C) y **arranca los dos firmwares reales del aparato**, volcados de un
iPod físico:

- **Rockbox** — arranca hasta el menú y se navega con la rueda.
- **El retailOS original de Apple** — pantalla de idioma, menú principal y
  navegación por submenús.

No se emula la ROM de arranque de Apple ni se ejecuta el firmware del chip de
vídeo: se modela el *hardware* que ambos firmwares tocan, y el *protocolo* del
chip de vídeo. El detalle está en [`wiki/`](wiki/).

> **Hace falta tu propio iPod.** Este repositorio **no** incluye firmware. Todo
> el material volcado vive en `assets/`, que está fuera de git a propósito: no
> es nuestro para redistribuirlo. Sin un volcado propio no hay nada que
> arrancar. La cadena completa para conseguirlo está más abajo.

## La prueba

Las capturas se generan con los dos scripts de regresión y quedan en `assets/`
(gitignorado). Estas cifras se midieron ejecutándolo el 2026-08-09; el número
que prueba algo es una diferencia **grande** de píxeles entre fotogramas: mover
el resaltado un elemento son ~14.000 y una pantalla nueva más de 18.000. Unos
pocos píxeles son el reloj o el icono de batería y no prueban nada.

### Rockbox — `tools/ipod_video/navtest.sh` (~55 s)

| Fotograma | Qué se ve | Píxeles distintos del anterior |
|---|---|---|
| `00-boot` | Rockbox arrancado | — |
| `01-menu` | menú principal, **Files** resaltado | unos cientos † |
| `02-wheel1` | (sin cambio, ver nota) | 0 |
| `03-wheel2` | resaltado bajando | **12.312** |
| `04-wheel3` | resaltado en **Resume Playback** | **12.114** |
| `05-wheelback` | (sin cambio, ver nota) | 0 |
| `06-enter` | pantalla **Now Playing** | **31.597** |

Los dos ceros son comportamiento real de Rockbox, no un fallo: reinicia su
acumulador de rueda al invertir el sentido, así que la primera pulsación tras
cambiar de dirección no mueve nada.

† **Las cifras pequeñas no son invariantes**: son el reloj de la barra de
estado y cambian con la hora a la que ejecutes. Dos ejecuciones del 2026-08-09
dieron 70 y 248 para ese mismo fotograma. Las grandes —las que prueban que la
navegación funciona— sí se repiten.

### retailOS de Apple — `tools/ipod_video/appletest.sh` (~3 min)

| Fotograma | Qué se ve | Píxeles distintos del anterior |
|---|---|---|
| `boot-010s` … `boot-120s` | pantalla de **idioma** del primer arranque, idéntica a 10/20/40/60/90/120 s | 0 entre sí |
| `nav-0-language` | *English* resaltado | — |
| `nav-1-wheel` | rueda: **no mueve nada aquí**, ver [límites](#límites-conocidos) | 0 |
| `nav-2-select` | **menú principal**: Music / Photos / Videos / Extras / Settings / Shuffle Songs | 8.049 |
| `nav-3-wheel` | resaltado bajado a **Videos** | 15.360 |
| `nav-4-enter` | submenú **Videos**: Video Playlists / Movies / Music Videos / Video Podcasts / Video Settings | 18.731 |
| `nav-5-menu` | vuelta al menú principal, *Videos* aún resaltado | 18.731 |

La pantalla de arranque son **75.267 de 76.800 píxeles encendidos (98,00 %)** a
320×240 RGB565, con 307 colores.

## Compilar

Se compila en WSL sobre un árbol que vive en Windows. Detalle completo y tres
trampas del entorno (CRLF, symlinks de git, `--disable-werror`) en
[`wiki/05-build-notes.md`](wiki/05-build-notes.md).

```bash
# Configurar (una vez)
cd /mnt/c/Desarrollo/qemu-ios && mkdir -p build && cd build
../configure --enable-sdl --disable-cocoa --target-list=arm-softmmu \
             --disable-capstone --disable-slirp \
             --extra-cflags=-I/usr/include/openssl --extra-ldflags=-lcrypto \
             --disable-werror --enable-pie

# Compilar
ninja qemu-system-arm
```

Dependencias en Ubuntu 24.04:

```bash
sudo apt install -y libsdl2-dev libssl-dev libglib2.0-dev libpixman-1-dev \
                    ninja-build python3-venv dosfstools mtools
```

Compilar desde cero lleva unos **16 minutos** (medido: configure ~3 min, ninja
~13 min). Un `ninja` sin nada que hacer tarda ~20 s en caliente, pero puede
tardar **6 minutos** la primera vez tras tocar ficheros desde Windows: es la
caché de 9p de `/mnt/c`, no un build roto.

## Ejecutar

Los dos firmwares salen de la **misma imagen** (`osos_full.bin`): el iPod es de
arranque dual y `ipodpatcher` dejó el bootloader de Rockbox detrás del firmware
de Apple. Sólo cambia dónde empieza la ejecución, que es lo que hace la
propiedad `entry`.

```bash
# Rockbox
./build/qemu-system-arm -M "iPod-Video,entry=0x735a00" \
    -kernel assets/images-5g/osos_full.bin \
    -drive if=ide,index=0,file=assets/ipod-video-disk.img,format=raw,file.locking=off \
    -snapshot -display sdl -serial null

# El retailOS de Apple: lo mismo con entry=0
```

- `entry=0x735a00` es el `entryOffset` del directorio de la partición de
  firmware **de este aparato**; puede ser otro en el tuyo. `fwtool.py` lo dice.
- `-snapshot` y `file.locking=off` evitan tocar la imagen y permiten varias
  instancias a la vez. Conviene usarlos siempre.
- Rockbox llega al menú sobre los **28 s**; la pantalla de idioma de Apple está
  a los **10 s**.

También hay un lanzador, `tools/ipod_video/run.sh rockbox|apple`, pensado para
trazar registros (`-d unimp,guest_errors`): **no engancha disco**, así que no
llega a la interfaz.

### Teclas

| Tecla | Botón |
|---|---|
| `w` | MENU |
| `a` | LEFT |
| `s` | PLAY |
| `d` | RIGHT |
| `Enter` | SELECT |
| `e` | rueda en sentido horario |
| `q` | rueda en sentido antihorario |

### Propiedades de la máquina

No aparecen en `-M iPod-Video,help` porque se registran en `instance_init`:

| Propiedad | Por defecto | Qué hace |
|---|---|---|
| `entry` | `0` | Offset dentro del `osos` donde empieza la ejecución |
| `remap` | `off` | Pone la DRAM en la dirección 0 desde el reset, sin esperar a que el firmware programe la unidad de remapeo |
| `semihosting` | `on` | Contesta al semihosting de ARM. **Déjalo puesto**: es como el retailOS imprime sus panics |

## Reproducirlo desde un iPod físico

Cadena verificada de principio a fin. Requiere un iPod Video 5G con Rockbox
instalado (el arranque dual es lo que da los dos firmwares en una imagen).

**1. Volcar la partición de firmware** — Windows, terminal de administrador:

```powershell
powershell -ExecutionPolicy Bypass -File tools\ipod_video\dump-firmware.ps1
```

Abre el disco en solo lectura y copia desde el sector 0 hasta el principio de
la partición FAT32. Windows no muestra la partición de firmware porque su tipo
MBR es `0x00`. Salen dos ficheros: el volcado completo (`ipod-video-fw.bin`,
con el MBR delante) y el mismo sin los 63 sectores de cabecera
(`ipod-video-fwpart.bin`).

**2. Extraer las imágenes del contenedor `]ih[`:**

```bash
python3 tools/ipod_classic/fwtool.py list    assets/ipod-video-fwpart.bin
python3 tools/ipod_classic/fwtool.py extract assets/ipod-video-fwpart.bin assets/images-5g
```

Ojo: aquí va el **`fwpart`**, no el volcado completo. Produce `osos_full.bin` y,
partido por el `entryOffset`, `apple_retailos.bin` y `rockbox_bootloader.bin`.

**3. Construir una imagen de disco** (MBR + firmware + una FAT32 con `.rockbox`):

```bash
./tools/ipod_video/mkdisk.sh assets/ipod-video-fw.bin <dir-con-.rockbox> \
                             assets/ipod-video-disk.img 256
```

Aquí sí va el volcado **completo**. Tarda ~1,4 s. Lo único que modifica del
volcado son 3 bytes de la tabla de particiones: el recuento de sectores de la
FAT32, para que el firmware no busque sectores que en la imagen no existen.

**4. Arrancar**, con los comandos de la sección anterior.

Opcionalmente, `tools/ipod_video/extract-vc.py` saca `vmcs.bin` (el firmware
del chip de vídeo) del FAT16 incrustado en la partición de firmware. La máquina
lo sube al chip emulado por DMA, pero **no lo ejecuta**.

## Qué está modelado

Todo esto está en `hw/arm/ipod_video/` (8 ficheros) e
`include/hw/arm/ipod_video/` (7 cabeceras).

| Bloque | Estado |
|---|---|
| **Dos núcleos ARM** (CPU + COP) | Se duermen y despiertan de verdad por `CPU_CTL`/`COP_CTL`; `PROCESSOR_ID` contesta según quién pregunte |
| **DRAM, IRAM, ventana NOR** | Más los alias del bit 26 y del bit 31 (la vista sin caché), que el retailOS forma y Rockbox no |
| **Unidad de remapeo** (`0xf000f000`) | Decodifica el par lógico/físico. El campo `mask` **no se aplica** |
| **Controlador de interrupciones** | Dos bancos de 32, máscara por núcleo, y la línea 30 sintetizada como resumen del banco alto. **Sin prioridad y sin FIQ** |
| **Timers** | `USEC_TIMER` libre y TIMER1/2 con interrupción. Leer la cuenta restante devuelve 0 |
| **IDE + DMA ATA** | Task file y motor DMA. Una sola unidad; el dispositivo no es migrable |
| **Click wheel** | 5 botones y la rueda, por teclado, con la interrupción que el firmware necesita |
| **I2C + PCF50605** | Transacción completa, códigos de resultado, ADC y RTC. Sin alarma, carga ni reguladores |
| **DMA del PP502x** | Los **dos** bloques, 8 canales cada uno. Sin líneas de petición de periférico |
| **BCM2722 + interfaz de host del VideoCore** | Protocolo de dos puertos, subida del firmware por DMA, dos canales (`gencmd` ASCII y RPC de display) y volcado a pantalla |
| **UART 16550** | TX/RX. **La interrupción no está conectada** |
| GPIO, caché, reloj/PLL, control de dispositivo | Ficheros de registros con los bits que el firmware espera |
| **Semihosting de ARM** | Activado por defecto |

## Qué **no** está modelado

- **Audio.** Ni I2S ni el códec WM8758. **No suena nada**, en ninguno de los
  dos firmwares.
- **USB.** Es un `unimplemented_device`.
- **El VideoCore.** Se modela su *protocolo*, no su procesador: el `vmcs.bin`
  real se sube al chip emulado y ahí se queda sin ejecutar. Tampoco hay TV-out
  ni blit YUV.
- **La ROM de arranque de Apple.** La máquina hace su trabajo (copiar el `osos`
  y saltar), y reproduce a mano lo que esa ROM dejaba en memoria: `GPO32` bit
  14 y el bloque SysInfo.
- **La caché** propiamente dicha, la prioridad de interrupciones, las FIQ, las
  interrupciones de GPIO y el buzón CPU-COP de `0x60001000`.

## Límites conocidos

Se listan porque el valor de esta documentación es que se pueda confiar en
ella.

- **La rueda no mueve el resaltado en la pantalla de idioma del retailOS.**
  Medido (0 píxeles de diferencia), **no diagnosticado**: no se sabe si es
  limitación del modelo o comportamiento del propio firmware. En el menú
  principal sí funciona.
- **`navtest.sh` y `appletest.sh` no son aserciones, son generadores de
  evidencia.** Ambos cierran la invocación de QEMU con `|| true` y el
  post-proceso no comprueba nada: un QEMU que muera al arrancar da un "pase"
  silencioso. **Hay que mirar las cifras.**
- **Los fotogramas de Rockbox no son reproducibles bit a bit**, porque su barra
  de estado lleva reloj. La diferencia contra una línea base son un par de
  cientos de píxeles y **la cifra exacta no es un invariante**. Los del
  retailOS sí son reproducibles: no dibuja reloj.
- **El tamaño de la DRAM es una cuestión abierta.** Están puestos 64 MB porque
  es lo que Rockbox lee del silicio, pero el propio firmware de Apple y la
  tabla de iPodLinux dicen 32. Las dos cifras funcionan.
- La máquina **`iPod-Classic`** (S5L8702) también está en este repo: ejecuta
  código, con timers e interrupciones verificados, pero **no arranca firmware
  real**, porque el del Classic 6G está cifrado con una clave que vive en el
  silicio. Fue el objetivo original del proyecto.
- Varias constantes del modelo son elecciones nuestras o inferencias, no datos
  confirmados. Van marcadas `ASSUMED` / `INFERRED` / `UNDETERMINED` en el
  código, y recogidas en [`wiki/09-historia-5g.md`](wiki/09-historia-5g.md).

## Dónde está el detalle

| Documento | Contenido |
|---|---|
| [`wiki/README.md`](wiki/README.md) | Índice de todo, y dónde vive cada cosa |
| [`wiki/07-pp502x-hardware.md`](wiki/07-pp502x-hardware.md) | **Referencia del PP5020/PP5022**: mapa de memoria, IRQs, los registros que tienen comportamiento de verdad, el protocolo del Broadcom |
| [`wiki/09-historia-5g.md`](wiki/09-historia-5g.md) | **Cómo se averiguó todo**: cada obstáculo, su síntoma y su causa, **incluidas las hipótesis que resultaron falsas** |
| [`wiki/04-roadmap.md`](wiki/04-roadmap.md) | Estado por hitos y el recorrido de cada bloqueo |
| [`wiki/05-build-notes.md`](wiki/05-build-notes.md) | Compilar, ejecutar, probar, y cómo averiguar dónde se ha quedado el firmware |
| [`wiki/06-ipod-video-5g.md`](wiki/06-ipod-video-5g.md) | El aparato real: identificación, layout del disco y su firmware sin cifrar |

Los documentos 01, 02, 03 y 08 son del trabajo sobre el iPod classic 6G.

## Este repositorio es un fork

Parte de [devos50/qemu-ios](https://github.com/devos50/qemu-ios) (QEMU 8.2.0),
rama `ipod_touch_2g`, que emula el **iPod Touch 1G y 2G**. Ese trabajo sigue
aquí intacto y sus máquinas se listan en `-M help`:

- [Cómo ejecutar el iPod Touch 1G](https://devos50.github.io/blog/2022/ipod-touch-qemu-pt2/)
  y el [artículo técnico](https://devos50.github.io/blog/2022/ipod-touch-qemu/).
- [Cómo ejecutar el iPod Touch 2G](https://github.com/devos50/qemu-ios/blob/ipod_touch_2g/RUNNING.md).

La documentación de QEMU está en [`docs/`](docs/); este README sustituye al del
fork original, cuyo contenido queda resumido arriba.

Fuentes de las que depende el trabajo del PP502x: **Rockbox** (drivers open
source a nivel de registro, `firmware/target/arm/ipod/` y `pp5020.h`),
**iPodLinux** (`ipodloader2/ipodhw.c`, de donde sale el bloque SysInfo) e
**ipodpatcher** (el modelo de arranque dual).
