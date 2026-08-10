# Dos iPods clickwheel en QEMU, arrancando el firmware real de Apple

Este fork de [QEMU-iOS](https://github.com/devos50/qemu-ios) añade **dos
máquinas** que emulan iPods de rueda de clic y **arrancan software real volcado
de los aparatos físicos**, sin ROM de arranque y sin parchear una sola
instrucción del firmware:

| Máquina | SoC | Qué arranca |
|---|---|---|
| **`iPod-Video`** | PortalPlayer PP5022C | **Rockbox** hasta el menú, navegable, y **el retailOS de Apple**: pantalla de idioma → menú principal → submenús → atrás |
| **`iPod-Classic`** | Samsung S5L8702 | **El retailOS de Apple**: kernel RTXC, disco, tipografías, y **la pantalla de selección de idioma, navegable con la rueda** |

En ninguna de las dos se emula la ROM de arranque de Apple: se modela el
*hardware* que el firmware toca y se reproduce a mano lo que esa ROM dejaba en
memoria. El detalle de todo está en [`wiki/`](wiki/).

> **Hace falta tu propio iPod.** Este repositorio **no** incluye firmware. Todo
> el material volcado vive en `assets/`, que está fuera de git a propósito: no
> es nuestro para redistribuirlo. Sin un volcado propio no hay nada que
> arrancar. Las dos cadenas para conseguirlo están más abajo.

---

## La prueba

Las capturas se generan con los scripts de regresión y quedan en `assets/`
(gitignorado), así que aquí van **las cifras y la orden que las regenera**. El
número que prueba algo es una diferencia **grande** de píxeles entre fotogramas.
Unos pocos píxeles son el reloj de la barra de estado y no prueban nada.

### iPod classic 6G — `tools/ipod_classic/navtest.sh` (~2 min)

Tres carreras consecutivas dieron cifras **idénticas al dígito**:

| Fotograma | Qué se ve | Píxeles encendidos | Distintos del anterior |
|---|---|---|---|
| `00-idle` | pantalla de idioma, **English** resaltado | 75 463 | — |
| `01-idle-again` | sin tocar nada: el suelo de ruido | 75 463 | **0** |
| `02-wheel-fwd` | `e`: el resaltado baja a **日本語** | 75 432 | **8 000** |
| `03-wheel-fwd2` | `e`: baja a **Čeština** | 75 463 | **8 000** |
| `04-wheel-back` | `q`: vuelve a **日本語** | 75 432 | **8 000** |
| `05-select` | Enter: la nota musical y **「ミュージックが…」** | 76 800 | **50 540** |
| `06-select-later` | 40 s sin tocar nada | 76 800 | **0** |
| `07-menu` | `w` (MENU): no hace nada, ver [límites](#límites-conocidos) | 76 800 | **0** |

Dos cosas que hacen que esta tabla signifique algo:

- **El suelo de ruido es exactamente 0**, y el test lo comprueba en cada carrera
  antes de creerse nada — esta pantalla no dibuja reloj. Si deja de ser 0, el
  test **suspende**, porque todos sus umbrales descansan en ese cero.
- La pantalla a la que entra `Enter` sale **en el idioma que estaba
  resaltado**. Eso es lo que convierte «cambiaron muchos píxeles» en «la
  interfaz hizo lo que se le pidió».

La pantalla de arranque son **75 463 de 76 800 píxeles encendidos (98,3 %)** a
320×240 RGB565, con 404 colores. Tarda entre **22 y 30 s** de reloj de pared
hasta el primer píxel.

### iPod Video 5G — `tools/ipod_video/navtest.sh` (~55 s), Rockbox

| Fotograma | Qué se ve | Distintos del anterior |
|---|---|---|
| `00-boot` | Rockbox arrancado | — |
| `01-menu` | menú principal, **Files** resaltado | unos cientos † |
| `02-wheel1` | (sin cambio, ver nota) | 0 |
| `03-wheel2` | resaltado bajando | **12 312** |
| `04-wheel3` | resaltado en **Resume Playback** | **12 114** |
| `05-wheelback` | (sin cambio, ver nota) | 0 |
| `06-enter` | pantalla **Now Playing** | **~31 600** ‡ |

Los dos ceros son comportamiento real de Rockbox: reinicia su acumulador de
rueda al invertir el sentido, así que la primera pulsación tras cambiar de
dirección no mueve nada.

† **Las cifras pequeñas no son invariantes**: son el reloj de la barra de
estado. Dos ejecuciones del mismo día dieron 70 y 248 para ese fotograma.

‡ Tampoco lo es la última: esa pantalla de Rockbox lleva reloj y el test pulsa
por reloj de pared. Se han medido 31 588, 31 598, 31 602, 31 609 y 31 632. Los
76 548 píxeles totales y los saltos de 12 312 / 12 114 **sí** son estables.

### iPod Video 5G — `tools/ipod_video/appletest.sh` (~3 min), retailOS

| Fotograma | Qué se ve | Distintos del anterior |
|---|---|---|
| `boot-010s` … `boot-120s` | pantalla de **idioma**, idéntica a 10/20/40/60/90/120 s | 0 entre sí |
| `nav-1-wheel` | la rueda **no mueve nada aquí**, ver [límites](#límites-conocidos) | 0 |
| `nav-2-select` | **menú principal**: Music / Photos / Videos / Extras / Settings | 8 049 |
| `nav-3-wheel` | resaltado bajado a **Videos** | 15 360 |
| `nav-4-enter` | submenú **Videos** | 18 731 |
| `nav-5-menu` | vuelta al menú principal, *Videos* aún resaltado | 18 731 |

Pantalla de arranque: **75 267 de 76 800 píxeles (98,00 %)**, 307 colores.

---

## Compilar

Se compila en WSL sobre un árbol que vive en Windows. **El binario es un ELF de
Linux: sólo se ejecuta bajo WSL.** Detalle completo y tres trampas del entorno
(CRLF, symlinks de git, `--disable-werror`) en
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

Compilar desde cero lleva unos **16 minutos**. Un `ninja` sin nada que hacer
tarda ~20 s en caliente, pero puede tardar **6 minutos** la primera vez tras
tocar ficheros desde Windows: es la caché de 9p de `/mnt/c`, no un build roto.

---

## Ejecutar el iPod classic 6G

Hacen falta dos cosas: el `osos` **descifrado** (ver
[la cadena](#reproducirlo-desde-un-ipod-classic-6g-físico)) y una imagen de
disco construida a partir de la partición de firmware del `.ipsw`.

```bash
# El disco. El FAT32 va VACIO a propósito: las tipografías están en la
# partición de FIRMWARE, no aquí.
truncate -s 64M ~/6g/fat32.img && mkfs.vfat -F 32 -n IPOD ~/6g/fat32.img
python3 tools/ipod_classic/mkdisk6g4k.py ~/6g/E.img \
        assets/extracted/Firmware-24.9.1.2 ~/6g/fat32.img

# Arrancar
cd build && ./qemu-system-arm -M iPod-Classic \
    -kernel ../assets/classic6g/osos_decrypted.bin -serial null \
    -drive if=ide,format=raw,file=$HOME/6g/E.img -snapshot
```

- **Sin `-drive` no hay imagen**, y eso es del huésped, no del modelo: la
  interfaz se muere buscando tipografías, que viven en la partición `rsrc` del
  firmware. Entra en un ciclo de caída y reinicio.
- `-serial null` mantiene limpia la consola. **El firmware habla por stderr**
  (semihosting): no lo descartes nunca.

### Teclas

| Tecla | Botón |
|---|---|
| `q` / `e` | rueda antihorario / horario |
| `Enter` | SELECT (botón central) |
| `w` | MENU |
| `a` / `d` | anterior / siguiente |
| `s` | PLAY / PAUSA |

Las mismas que en la máquina del 5G, para que una sola costumbre valga para las
dos.

### Propiedades y variables de entorno

| Propiedad | Por defecto | Qué hace |
|---|---|---|
| `bootrom` | *(ninguna)* | Ruta a un binario de bootrom del S5L8702. Si se da, se queda con la dirección 0 en vez del alias de IRAM0 — **no tenemos ninguno**, ver [`wiki/10`](wiki/10-classic6g-emcore.md) |
| `semihosting` | `on` | Contesta al semihosting de ARM. **Déjalo puesto**: es como el retailOS imprime sus panics |

No hay propiedad `entry`: el `osos` descifrado se carga crudo en `0x08000000`,
que es su `addr` en el directorio del firmware.

| Variable | Qué traza |
|---|---|
| `DISPTRACE=1` | el compositor de `0x38900000`, con el PC del huésped y totales |
| `LCDTRACE=1` | el panel: órdenes, píxeles y arranques del motor de trama |
| `ATATRACE=1` | toda la conversación con el ATA, registro a registro |
| `WHEELTRACE=1` | paquetes, órdenes y flancos de la IRQ 23 |
| `GPIOTRACE=1` | accesos a GPIO |
| `IPOD_WHEEL_STEP` (6), `IPOD_WHEEL_TICK_MS` (40), `IPOD_WHEEL_UNTOUCH_MS` (1500) | ajustan la rueda para experimentar. Ninguno es un dato del aparato |

---

## Ejecutar el iPod Video 5G

Los dos firmwares salen de la **misma imagen** (`osos_full.bin`): el iPod es de
arranque dual y `ipodpatcher` dejó el bootloader de Rockbox detrás del firmware
de Apple. Sólo cambia dónde empieza la ejecución, que es la propiedad `entry`.

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

Teclas: **las mismas que en el Classic** (tabla de arriba).

| Propiedad | Por defecto | Qué hace |
|---|---|---|
| `entry` | `0` | Offset dentro del `osos` donde empieza la ejecución |
| `remap` | `off` | Pone la DRAM en la dirección 0 desde el reset, sin esperar a que el firmware programe la unidad de remapeo |
| `semihosting` | `on` | Igual que en el Classic |

No aparecen en `-M iPod-Video,help` porque se registran en `instance_init`.

También hay un lanzador, `tools/ipod_video/run.sh rockbox|apple`, pensado para
trazar registros: **no engancha disco**, así que no llega a la interfaz.

---

## Reproducirlo desde un iPod classic 6G físico

El bloqueo fundacional del proyecto era que el `osos` del Classic va cifrado con
la clave GID fundida en el silicio. **No hace falta extraer la clave: basta con
que descifre el propio aparato.** Cadena verificada, detalle completo en
[`wiki/10-classic6g-emcore.md`](wiki/10-classic6g-emcore.md).

**1. Sacar el `Firmware-XX` del `.ipsw`** (es un zip) y extraer sus cuatro
imágenes del contenedor `]ih[`:

```bash
python3 tools/ipod_classic/fwtool.py list    assets/extracted/Firmware-24.9.1.2
python3 tools/ipod_classic/fwtool.py extract assets/extracted/Firmware-24.9.1.2 assets/images
```

El `osos` que sale de ahí está **cifrado**. La partición `rsrc` no: es un
volumen FAT16 de 78 MB con `Resources/Fonts` dentro, y de ahí salen las nueve
escrituras de la pantalla de idioma.

**2. Instalar emCORE** en el iPod (r859 o similar, el bootloader de freemyipod).

**3. Descifrar el `osos` con el motor AES del propio SoC**, por USB:

```bash
python3 tools/ipod_classic/emcore_s5l8702.py info
python3 tools/ipod_classic/emcore_s5l8702.py decrypt \
        assets/images/1_osos.payload assets/classic6g/osos_decrypted.bin
```

Tarda **1,2 s**. Ninguno de los dos clientes oficiales de emCORE funciona tal
cual (el de HEAD usa un protocolo posterior a r891; el de r859 llama a pyusb con
un parámetro que desapareció en 1.0.0b1), y hay una trampa que hay que conocer
para trocear: `hwkeyaes()` **pone el IV a cero al principio de cada llamada**,
así que el primer bloque de cada trozo sale mal — en CBC se repara con un XOR
contra el último bloque de cifrado del trozo anterior.

Cómo saber que salió bien: **42 014 cadenas ASCII frente a 0**, vectores ARM
coherentes, `"Copyright 2001-2008 Apple Inc."`, `RTXC v3.2b`, y la interfaz
literal (*Cover Flow*, *Now Playing*).

**4. Construir el disco y arrancar**, con los comandos de la sección anterior.

---

## Reproducirlo desde un iPod Video 5G físico

Requiere un 5G con Rockbox instalado: el arranque dual es lo que da los dos
firmwares en una sola imagen.

**1. Volcar la partición de firmware** — Windows, terminal de administrador:

```powershell
powershell -ExecutionPolicy Bypass -File tools\ipod_video\dump-firmware.ps1
```

Abre el disco en solo lectura y copia desde el sector 0 hasta el principio de la
partición FAT32. Windows no muestra la partición de firmware porque su tipo MBR
es `0x00`. Salen dos ficheros: el volcado completo (`ipod-video-fw.bin`, con el
MBR delante) y el mismo sin los 63 sectores de cabecera
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

Aquí sí va el volcado **completo**. Lo único que modifica son 3 bytes de la
tabla de particiones: el recuento de sectores de la FAT32.

Opcionalmente, `tools/ipod_video/extract-vc.py` saca `vmcs.bin` (el firmware del
chip de vídeo) del FAT16 incrustado en la partición de firmware. La máquina lo
sube al chip emulado por DMA, pero **no lo ejecuta**.

---

## Qué está modelado

### iPod classic 6G — `hw/arm/ipod_classic/` (10 ficheros) e `include/` (10 cabeceras)

| Bloque | Estado |
|---|---|
| **ARM926EJ-S, SDRAM, IRAM0/1** | Más el alias de IRAM0 en la dirección 0, de donde el ARM926 toma sus excepciones |
| **VIC0/VIC1** (PL192 de serie) | `0x38E00000` / `0x38E01000`, 64 líneas |
| **Timers** (`0x3C700000`) | 8 canales; A–D de 16 bits en IRQ 8, E–H de 32 bits en IRQ 7. Sólo modo intervalo. **PCLK se trata como ECLK** |
| **Reloj / PLL** (`0x3C500000`) | Escrito para este chip: `SWRCON`, `RSTSR`, `PLLMOD2`. `PLLLOCK` contesta bloqueado, `SWRCON` reinicia |
| **I²C0 + PCF50635** (`0x3C600000`) | Transacción completa con estado y control en registros distintos, byte de 25 µs, ADC, INT1..6 de lectura-limpia. **Sin alarma, carga ni reguladores** |
| **ATA + DMA** (`0x38700000`) | Puente de Apple, no un *task file* directo. Lee de verdad, verificado leyendo la memoria del huésped |
| **Compositor de vídeo** (`0x38900000`) | **Lo que dibuja.** Cinco descriptores de capa con stride, formato, dirección de origen, tamaño y posición. Ninguna fuente externa nombra este bloque |
| **Panel LCD** (`0x38300000`) | 320×240 RGB565, ventana `0x2A`/`0x2B`, `0x2C`, `qemu_console`, y el motor de trama de `+0x70..+0x8C` |
| **DMAC0 / DMAC1** (PL080 de serie) | IRQ 16 y 17, con la memoria del sistema como puerto maestro |
| **Click wheel** (`0x3C200000`) | IRQ 23, los dos formatos de paquete, cinco botones y la rueda, por teclado, con el ritmo que el decodificador del firmware exige |
| **GPIO** (`0x3CF00000`) | Un pin modelado, el 86, que la tarea de almacenamiento traduce a «hay disco» |
| **RNG** (`0x3C100000`) | Identidad `INFERIDA`; contrato de tres registros verificado. **Sin bit de habilitación**, y eso es un dato |
| **UART0** (`0x3CC00000`) | `exynos4210_uart` |
| **Bloque de parámetros del bootrom** | Los tres campos que el arranque lee, y ni uno más |
| **Semihosting de ARM** | Activado por defecto |
| Los demás 24 periféricos | `create_unimplemented_device()`, más un **suelo de E/S** bajo los 768 MB de la ventana |

### iPod Video 5G — `hw/arm/ipod_video/` (8 ficheros) e `include/` (7 cabeceras)

| Bloque | Estado |
|---|---|
| **Dos núcleos ARM** (CPU + COP) | Se duermen y despiertan de verdad; `PROCESSOR_ID` contesta según quién pregunte |
| **DRAM, IRAM, ventana NOR** | Más los alias del bit 26 y del bit 31 (la vista sin caché), que el retailOS forma y Rockbox no |
| **Unidad de remapeo** (`0xf000f000`) | Decodifica el par lógico/físico. El campo `mask` **no se aplica** |
| **Controlador de interrupciones** | Dos bancos de 32, máscara por núcleo, y la línea 30 sintetizada como resumen del banco alto. **Sin prioridad y sin FIQ** |
| **Timers** | `USEC_TIMER` libre y TIMER1/2 con interrupción |
| **IDE + DMA ATA** | Task file y motor DMA. Una sola unidad |
| **Click wheel** | 5 botones y la rueda, por teclado, con su interrupción |
| **I²C + PCF50605** | Transacción completa, códigos de resultado, ADC y RTC |
| **DMA del PP502x** | Los **dos** bloques, 8 canales cada uno |
| **BCM2722 + interfaz de host del VideoCore** | Protocolo de dos puertos, subida del firmware por DMA, dos canales (`gencmd` ASCII y RPC de display) y volcado a pantalla |
| **UART 16550** | TX/RX. **La interrupción no está conectada** |
| GPIO, caché, reloj/PLL, control de dispositivo | Ficheros de registros con los bits que el firmware espera |

---

## Límites conocidos

Se listan porque el valor de esta documentación es que se pueda confiar en ella.

**iPod classic 6G**

- **MENU no vuelve atrás.** Desde la pantalla que sigue a elegir idioma, `w` da
  0 píxeles de diferencia. La tecla **llega entera** —construye el mensaje
  `'kDwn'` y entra en la cola del EventManager— y esa pantalla **no está sorda**
  (reacciona a un giro). Que sea porque el asistente de primer arranque no tiene
  a dónde volver es `INFERIDO`. **Es el hito que falta.**
- **El panel derecho no cambia al girar**, cuando la regla de la interfaz dice
  que debería. Apunta al `TimerEventManager`.
- **Sin disco no hay imagen**, y es irreparable por ese camino: no hay ninguna
  tipografía dentro de la imagen del firmware.
- **Sin audio, sin USB, sin NAND, sin CE-ATA, sin RTC, sin AES.**
- **La composición de varias capas nunca se ha visto en una captura**: la capa 0
  sola ya contiene la pantalla entera, y el barrido de la segunda dura menos de
  un segundo. Los 75 463 píxeles prueban la primera ranura y no el orden Z.

**iPod Video 5G**

- **La rueda no mueve el resaltado en la pantalla de idioma del retailOS.**
  Medido (0 píxeles), **no diagnosticado**. En el menú principal sí funciona.
- **Sin audio** (ni I²S ni el códec WM8758) y **sin USB**.
- **El VideoCore no se ejecuta**: se modela su protocolo, no su procesador.
- **El tamaño de la DRAM es una cuestión abierta**: 64 MB porque es lo que
  Rockbox lee del silicio, aunque el propio firmware y la tabla de iPodLinux
  digan 32. Las dos cifras funcionan.

**De los dos**

- **Varias constantes son elecciones nuestras o inferencias**, no datos
  confirmados. Van marcadas `ASSUMED` / `INFERRED` / `UNDETERMINED` en el
  código, y recogidas en [`wiki/09`](wiki/09-historia-5g.md) y
  [`wiki/12`](wiki/12-historia-6g.md).
- **`tools/ipod_video/navtest.sh` y `appletest.sh` no son aserciones, son
  generadores de evidencia**: cierran la invocación de QEMU con `|| true` y no
  comprueban nada. **Hay que mirar las cifras.**
  `tools/ipod_classic/navtest.sh` **sí** decide, y suspende si el suelo de ruido
  deja de ser 0.

---

## Dónde está el detalle

| Documento | Contenido |
|---|---|
| [`wiki/README.md`](wiki/README.md) | Índice de todo, y dónde vive cada cosa |
| [`wiki/12-historia-6g.md`](wiki/12-historia-6g.md) | **Cómo se averiguó todo en el Classic 6G**: cada obstáculo, su síntoma y su causa, el catálogo de hipótesis retiradas, y el capítulo de **las tres veces que un descarte era la pista buena** |
| [`wiki/09-historia-5g.md`](wiki/09-historia-5g.md) | Lo mismo para el iPod Video 5G |
| [`wiki/11-classic6g-retailos.md`](wiki/11-classic6g-retailos.md) | **El retailOS del Classic corriendo**, sección a sección: el compositor, el ATA, la rueda, y cómo reproducir cada medida |
| [`wiki/10-classic6g-emcore.md`](wiki/10-classic6g-emcore.md) | **El aparato real por USB** y el descifrado del `osos` con la clave GID del propio SoC |
| [`wiki/07-pp502x-hardware.md`](wiki/07-pp502x-hardware.md) | **Referencia del PP5020/PP5022** |
| [`wiki/02-s5l8702-hardware.md`](wiki/02-s5l8702-hardware.md) | **Referencia del S5L8702**, con qué está modelado y qué no |
| [`wiki/04-roadmap.md`](wiki/04-roadmap.md) | Estado por hitos y el recorrido de cada bloqueo |
| [`wiki/05-build-notes.md`](wiki/05-build-notes.md) | Compilar, ejecutar, probar, y cómo averiguar dónde se ha quedado el firmware |

---

## Este repositorio es un fork

Parte de [devos50/qemu-ios](https://github.com/devos50/qemu-ios) (QEMU 8.2.0),
rama `ipod_touch_2g`, que emula el **iPod Touch 1G y 2G**. Ese trabajo sigue
aquí intacto y sus máquinas se listan en `-M help`:

- [Cómo ejecutar el iPod Touch 1G](https://devos50.github.io/blog/2022/ipod-touch-qemu-pt2/)
  y el [artículo técnico](https://devos50.github.io/blog/2022/ipod-touch-qemu/).
- [Cómo ejecutar el iPod Touch 2G](https://github.com/devos50/qemu-ios/blob/ipod_touch_2g/RUNNING.md).

La documentación de QEMU está en [`docs/`](docs/); este README sustituye al del
fork original.

Fuentes de las que depende este trabajo: **Rockbox** (drivers open source a
nivel de registro para los dos aparatos — `firmware/target/arm/s5l8702/` y
`firmware/export/s5l87xx.h` para el Classic, `firmware/target/arm/ipod/` y
`pp5020.h` para el 5G), **iPodLinux** (`ipodloader2/ipodhw.c`, de donde sale el
bloque SysInfo), **ipodpatcher** (el modelo de arranque dual) y
**[freemyipod.org](https://freemyipod.org/wiki/Classic_6G)** (emCORE, y un
modelo QEMU del S5L8702 en `freemyipod/qemu@s5l8702-dev`).
