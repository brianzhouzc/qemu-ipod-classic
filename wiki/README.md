# Emulación de iPods clickwheel en QEMU

Bitácora de ingeniería inversa y desarrollo del port de QEMU-iOS. Rama de
trabajo: `ipod_classic_6g`, partiendo de `ipod_touch_2g` (QEMU 8.2.0).

Empezó apuntando al **iPod classic 6G** (S5L8702) y se topó con que su firmware
está cifrado con una clave que vive en el silicio. Al aparecer un **iPod Video
5G** físico, cuyo firmware está en claro, el objetivo de arrancar software real
se movió allí. El 9 de agosto de 2026 el bloqueo del Classic cayó: no hacía
falta extraer la clave, sino que **descifrara el propio aparato** — el iPod
físico corriendo emCORE suelta el `osos` en claro por USB. Y el 10 de agosto el
Classic pasó de arrancar a **verse** y de verse a **navegarse**.

**Hoy las dos máquinas arrancan el firmware original de Apple y las dos se
navegan.**

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Video` | PP5022C | **Arranca los dos firmwares del aparato real**: Rockbox hasta el menú, navegable; y el retailOS de Apple dibujando su interfaz, con menús y submenús navegables |
| `iPod-Classic` | S5L8702 | **Arranca el retailOS de Apple**: kernel RTXC, ATA con DMA, volumen `rsrc` montado, tipografías, y **la pantalla de selección de idioma — 75 463 de 76 800 píxeles — que la rueda mueve y el botón central atraviesa**. MENU todavía no vuelve atrás |

Para una introducción de una página, empezar por el
[README de la raíz](../README.md). Para compilarlo, ejecutarlo y probarlo:
[05-build-notes.md](05-build-notes.md).

## Índice

### Cómo se averiguó todo — los dos documentos que más rinden

| Documento | Contenido |
|---|---|
| [12-historia-6g.md](12-historia-6g.md) | **Cronología técnica del iPod classic 6G**: desde un firmware que era un bloque cifrado ilegible hasta una pantalla de idiomas navegable. Cada obstáculo con su síntoma, su causa y **cómo se encontró**; el catálogo de hipótesis retiradas; y el capítulo de **las tres veces que un descarte resultó ser la pista buena** — qué tienen en común y qué habría que haber hecho distinto. |
| [09-historia-5g.md](09-historia-5g.md) | Lo mismo para el iPod Video 5G: cada obstáculo, su síntoma y su causa, **incluidas las hipótesis que resultaron falsas** y por qué se sostuvieron. Con el catálogo de errores retirados, las técnicas de depuración ordenadas por lo que rindieron, y las cuestiones abiertas. |

### El iPod classic 6G (S5L8702)

| Documento | Contenido |
|---|---|
| [10-classic6g-emcore.md](10-classic6g-emcore.md) | **El aparato real por USB**: el protocolo de emCORE r859 (y por qué los clientes oficiales no funcionan), **el descifrado del `osos` con la clave GID del propio SoC** — el bloqueo fundacional, resuelto —, el volcado de la NOR con su bloque `SCfg`, la prueba de que **la bootrom está bloqueada** y no se puede volcar por esta vía, y la dirección que cuelga el monitor. |
| [11-classic6g-retailos.md](11-classic6g-retailos.md) | **El retailOS corriendo**, 17 secciones: las dos regiones de enlace de la imagen, el bloque sin nombre de `0x3C100000`, el panel y el puente ATA, el ciclo de caída sin disco y su causa, **el compositor de `0x38900000` y de dónde salen los píxeles** (§13), **por qué el arranque con disco estaba parado: DMAC0, no el ATA** (§14), **la rueda de clic y la pantalla que se puede mover** (§17), y tres auditorías independientes. |
| [01-firmware-ipsw.md](01-firmware-ipsw.md) | Formato del `.ipsw`, la partición de firmware `]ih[` y la cabecera de imagen `8702`. Incluye la corrección que costó días: **`rsrc` no está vacío, es un FAT16 con las tipografías** que la interfaz necesita. |
| [02-s5l8702-hardware.md](02-s5l8702-hardware.md) | **Referencia del S5L8702**: mapa de memoria, periféricos, números de IRQ y componentes de placa, **con lo que está modelado en este árbol y lo que no**. Corregido en `e17924eab`: la primera versión coló bases que sólo existen en S5L8700/8701. |
| [08-s5l8702-fuentes-externas.md](08-s5l8702-fuentes-externas.md) | Correcciones al mapa de memoria, bootrom, cadena de arranque, click wheel, y el **modelo QEMU del S5L8702 que ya existe** en `freemyipod/qemu`. |
| [03-reuse-qemu-ios.md](03-reuse-qemu-ios.md) | Documento de **planificación, histórico**: qué modelos de la rama `ipod_touch_2g` se pensaba reaprovechar. Lleva un banner con lo que de verdad se reutilizó. |

### El iPod Video 5G (PP502x)

| Documento | Contenido |
|---|---|
| [06-ipod-video-5g.md](06-ipod-video-5g.md) | **El aparato real**: identificación (y el conflicto de fuentes sobre qué chip lleva), layout del disco, su firmware **sin cifrar**, el arranque dual, y cómo volcar la flash ROM desde el propio Rockbox. |
| [07-pp502x-hardware.md](07-pp502x-hardware.md) | **Referencia del PP5020/PP5022**: mapa de memoria con qué está modelado y qué no, los alias del bit 26 y del bit 31, IRQs, los registros que tienen comportamiento, el IDE con su DMA, el I²C y el PCF50605, los dos bloques de DMA, el Broadcom BCM2722 y la interfaz de host del VideoCore, el click wheel y el modelo de arranque. Termina con **los supuestos del modelo que no son datos**. |

### Transversal

| Documento | Contenido |
|---|---|
| [04-roadmap.md](04-roadmap.md) | Estado por hitos y el recorrido de cada bloqueo que hubo que quitar. El documento más largo y el más cercano a un diario. |
| [05-build-notes.md](05-build-notes.md) | Cómo compilar (WSL), **cómo ejecutarlo y probarlo**, teclas del click wheel, las propiedades de máquina que no salen en `-M help`, tres trampas del entorno (CRLF, symlinks de git, `--disable-werror`) y cómo averiguar dónde se ha quedado el firmware. |

## Fuentes

- **Rockbox** — port completo y funcional a los dos aparatos, con drivers open
  source a nivel de registro. Es la fuente primaria del mapa de memoria.
  - Para el PP502x: `firmware/target/arm/ipod/`, `firmware/export/pp5020.h`,
    `bootloader/ipod.c`.
  - Para el S5L8702: `firmware/export/s5l87xx.h` (2052 líneas, ~1400 defines),
    `firmware/target/arm/s5l8702/`, `bootloader/ipod-s5l87xx.c`.
  - Copia local en `assets/ref/` y `assets/rockbox/`.
  - **Aviso de método**: lo que un driver de Rockbox *nombra pero no usa* es
    justo donde el modelo tiene un agujero invisible. Ver `HI_IRQ` en
    [09-historia-5g.md](09-historia-5g.md).
- **iPodLinux** — `ipodloader2/ipodhw.c`, de donde sale el bloque SysInfo que
  la ROM de arranque deja en lo alto de la IRAM.
- **ipodpatcher** — el modelo de arranque dual y la tabla `getmodel()`.
- **freemyipod.org** — wiki de ingeniería inversa de los iPod no-iOS,
  [Classic_6G](https://freemyipod.org/wiki/Classic_6G); **emCORE**, que es lo
  que hace descifrable el firmware del Classic; y `freemyipod/qemu` rama
  `s5l8702-dev`, un modelo QEMU del mismo SoC.
- **devos50/qemu-ios** — el emulador base, ramas `ipod_touch_1g` (S5L8900) y
  `ipod_touch_2g` (S5L8720).
- **Blogs de Martijn de Vos** — [Touch 1G](https://devos50.github.io/blog/2022/ipod-touch-qemu/),
  [Touch 2G](https://devos50.github.io/blog/2023/ipod-touch-2g-qemu/).

## Dónde está cada cosa

```
hw/arm/ipod_video/              modelos del PP502x — 8 ficheros
include/hw/arm/ipod_video/      sus cabeceras — 7 ficheros
tools/ipod_video/               herramientas del 5G

hw/arm/ipod_classic/            modelos del S5L8702 — 10 ficheros
include/hw/arm/ipod_classic/    sus cabeceras — 10 ficheros
tools/ipod_classic/             herramientas del Classic

wiki/                           esta documentación
assets/                         GITIGNORADO — firmware, imágenes extraídas,
                                capturas, fuentes de referencia de Rockbox
```

`assets/` está fuera de git a propósito: es material que no es nuestro para
redistribuir. Sin un volcado propio de un iPod no hay nada que arrancar.
Contiene, entre otras cosas:

| Ruta | Qué es |
|---|---|
| `assets/classic6g/osos_decrypted.bin` | **el retailOS del Classic 6G en claro**, descifrado por el propio aparato. Es lo que arranca `-M iPod-Classic` |
| `assets/classic6g/bootflash_nor_1MB.bin` | volcado de la NOR SPI del Classic, con su bloque `SCfg` (de donde sale el `HwVr` = 19) |
| `assets/classic6g/aupd_decrypted.bin`, `rsrc_fat16.img` | el updater descifrado y el volumen de recursos |
| `assets/iPod_24.1.1.2.ipsw`, `assets/extracted/`, `assets/images/` | el firmware del Classic 6G, desempaquetado y extraído |
| `assets/ipod-video-fw.bin` | volcado de la partición de firmware del 5G, **con el MBR delante** (82 252 800 B) |
| `assets/ipod-video-fwpart.bin` | el mismo **sin** los 63 sectores de cabecera — es el que quiere `fwtool.py` |
| `assets/ipod-video-disk.img` | imagen de disco construida con `mkdisk.sh` (MBR + firmware + FAT32) |
| `assets/images-5g/` | `osos_full.bin`, `apple_retailos.bin`, `rockbox_bootloader.bin`, `rockbox_app.bin`, `vmcs.bin` |
| `assets/apple/`, `assets/nav-baseline/`, `assets/nav*/` | capturas de referencia de los dos aparatos |
| `assets/ref/`, `assets/rockbox/` | fuentes de Rockbox para S5L8702 e ipod6g |

## Herramientas propias

### `tools/ipod_classic/` — el Classic

| Herramienta | Qué hace |
|---|---|
| `emcore_s5l8702.py` | Habla el protocolo de emCORE r859 con el iPod físico por USB: `info`, `hexdump`, `dump`, `norread` y **`decrypt`**, que es la que saca el `osos` en claro con la clave GID del SoC |
| `fwtool.py` | Parsea y extrae las imágenes de un contenedor `]ih[`. **Se usa también para el 5G** |
| `mkdisk6g4k.py` | **Fabrica el disco arrancable** a partir del `Firmware-XX` del `.ipsw` + una FAT32 vacía, con el MBR escrito **en unidades de 4096 bytes**, que es el bloque de dispositivo de un iPod Classic. `mkdisk6g.py` es la variante en sectores de 512, de la investigación del layout |
| `mkfwpart6g.py` | Re-maqueta una partición de firmware para otro tamaño de bloque |
| `navtest.sh` | **La prueba de navegación**: arranca, espera, comprueba que la pantalla está quieta, pulsa, captura y **decide**. Suspende si el suelo de ruido deja de ser 0 |
| `audit_run.sh`, `audit_peek.sh` | Arrancan, esperan, capturan y resumen stderr; el segundo lee memoria física por el monitor |
| `gdbpoke.py`, `gdbrun.sh`, `gdbtrace.py` | Cliente del protocolo remoto de GDB: breakpoints, registros, memoria y `--skip` para saltarse una llamada **sin tocar la imagen del huésped**. `gdbrun.sh` arranca QEMU con `-S -gdb` y lo conduce |
| `pixcount.py`, `ppm2png.py` | Cuentan píxeles no negros de un `screendump` y lo convierten a PNG **para mirarlo**. `ppm2png.py --raw 320x240:argb8888` también lee framebuffers crudos sacados con `pmemsave` |
| `findlit.py` | Dónde está un literal de 32 bits en la imagen, en VA, y qué `ldr rX,[pc,#imm]` lo cargan. Sin falsos positivos |
| `lcdtest.S` | **Caso de control del panel**: dibuja una rampa de 320×240 que da 76 800 píxeles y 1920 colores exactos |
| `osana.py` | Ayudas de análisis del `osos_decrypted.bin`, con la base `0x07FF51B8` de la carga dispersa ya aplicada |
| `norboot_im3.py` | Identifica y descifra la imagen IM3 del offset `0x8000` de la NOR |
| `mktestbin.py` | Genera binarios ARM de prueba |
| `ranaddr.sh` | «¿corrió esta función?» — `-d in_asm` + grep, **sólo válido para entradas de bloque**, y el propio script lo avisa |
| `atatrace.sh` | Arranca con un disco y captura cada acceso a registro del ATA más lo que el firmware diga por semihosting |
| `mk6g_*.sh` | Herramientas de usar y tirar de la investigación del layout de disco |
| `tests/` | Tests bare-metal (timers, ruta de interrupción) |

### `tools/ipod_video/` — el 5G

| Herramienta | Qué hace |
|---|---|
| `dump-firmware.ps1` | Vuelca la partición de firmware de un iPod conectado. **Windows, como administrador.** Abre el disco en solo lectura y se niega si hay más de un candidato o si es el disco de sistema |
| `mkdisk.sh` | Construye una imagen de disco arrancable: MBR + firmware + una FAT32 con `.rockbox`. Necesita `dosfstools` y `mtools` |
| `navtest.sh` | Prueba de navegación de **Rockbox** (regresión contra `assets/nav-baseline`) |
| `appletest.sh` | Prueba de navegación del **retailOS de Apple** |
| `run.sh` | Lanzador (`run.sh rockbox\|apple`). Pensado para trazar registros: **no engancha disco** |
| `extract-vc.py` | Saca `vmcs.bin` y el resto del FAT16 incrustado en la partición de firmware |
| `findlit.py` | Igual que el del Classic. Nació de un error de método: buscar bytes crudos daba 139 falsos positivos donde había 1 verdadero |

> `tools/ipod_video/navtest.sh` y `appletest.sh` **no son aserciones**: salen con
> código 0 aunque QEMU muera al arrancar. Hay que mirar las cifras.
> `tools/ipod_classic/navtest.sh` **sí** decide, y suspende.
