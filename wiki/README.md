# Emulación de iPods clickwheel en QEMU

Bitácora de ingeniería inversa y desarrollo del port de QEMU-iOS. Rama de
trabajo: `ipod_classic_6g`, partiendo de `ipod_touch_2g` (QEMU 8.2.0).

Empezó apuntando al **iPod classic 6G** (S5L8702) y se topó con que su firmware
está cifrado con una clave que vive en el silicio. Al aparecer un **iPod Video
5G** físico, cuyo firmware está en claro, el objetivo de arrancar software real
se movió allí.

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Video` | PP5022C | **arranca los dos firmwares del aparato real: Rockbox hasta el menú, y el retailOS de Apple dibujando su interfaz y navegable** |
| `iPod-Classic` | S5L8702 | ejecuta código; timers e interrupciones verificados. **No arranca firmware real** (cifrado) |

Para una introducción de una página, empezar por el
[README de la raíz](../README.md). Para ejecutarlo y probarlo:
[05-build-notes.md](05-build-notes.md).

## Índice

### El iPod Video 5G (PP502x) — lo que arranca

| Documento | Contenido |
|---|---|
| [06-ipod-video-5g.md](06-ipod-video-5g.md) | **El aparato real**: identificación (y el conflicto de fuentes sobre qué chip lleva), layout del disco, su firmware **sin cifrar**, el arranque dual, y cómo volcar la flash ROM desde el propio Rockbox. |
| [07-pp502x-hardware.md](07-pp502x-hardware.md) | **Referencia del PP5020/PP5022**: mapa de memoria con qué está modelado y qué no, los alias del bit 26 y del bit 31, IRQs (y cuáles se conducen de verdad), los registros que tienen comportamiento, el IDE con su DMA, el I²C y el PCF50605, los dos bloques de DMA, el Broadcom BCM2722 y la interfaz de host del VideoCore, el click wheel y el modelo de arranque. Termina con **los supuestos del modelo que no son datos** y las diferencias conocidas entre modelo y hardware. |
| [09-historia-5g.md](09-historia-5g.md) | **Cómo se averiguó todo**: cronología de cada obstáculo, su síntoma y su causa, **incluidas las hipótesis que resultaron falsas** y por qué se sostuvieron. Con el catálogo de errores retirados, las técnicas de depuración ordenadas por lo que rindieron, y las cuestiones que siguen abiertas. |
| [04-roadmap.md](04-roadmap.md) | Estado, el recorrido de cada bloqueo que hubo que quitar, y los hitos (que son los del Classic). El documento más largo y el más cercano a un diario. |

### El iPod classic 6G (S5L8702) — el objetivo original

| Documento | Contenido |
|---|---|
| [01-firmware-ipsw.md](01-firmware-ipsw.md) | Formato del `.ipsw`, la partición de firmware `]ih[` y la cabecera de imagen `8702`. Qué contiene el firmware que tenemos. |
| [02-s5l8702-hardware.md](02-s5l8702-hardware.md) | Mapa de memoria, periféricos, números de IRQ y componentes de placa del S5L8702. Corregido en `e17924eab`: la primera versión coló bases que sólo existen en S5L8700/8701. |
| [03-reuse-qemu-ios.md](03-reuse-qemu-ios.md) | Qué modelos de dispositivo de la rama `ipod_touch_2g` se pueden reaprovechar y cuáles hay que escribir de cero. |
| [08-s5l8702-fuentes-externas.md](08-s5l8702-fuentes-externas.md) | Correcciones al mapa de memoria, bootrom, cadena de arranque, click wheel, y el **modelo QEMU del S5L8702 que ya existe** en `freemyipod/qemu`. Leerlo antes de escribir nada nuevo del Classic. |

### Transversal

| Documento | Contenido |
|---|---|
| [05-build-notes.md](05-build-notes.md) | Cómo compilar (WSL), **cómo ejecutarlo y probarlo**, teclas del click wheel, las propiedades de máquina que no salen en `-M help`, tres trampas del entorno (CRLF, symlinks de git, `--disable-werror`) y cómo averiguar dónde se ha quedado el firmware. |

## Fuentes

- **Rockbox** — port completo y funcional a los dos aparatos, con drivers open
  source a nivel de registro. Es la fuente primaria del mapa de memoria.
  - Para el PP502x: `firmware/target/arm/ipod/`, `firmware/export/pp5020.h`,
    `bootloader/ipod.c`.
  - Para el S5L8702: `firmware/export/s5l87xx.h` (2052 líneas, ~1400 defines),
    `firmware/target/arm/s5l8702/`, `bootloader/ipod-s5l87xx.c`.
  - Copia local en `assets/ref/`.
  - **Aviso de método**: lo que un driver de Rockbox *nombra pero no usa* es
    justo donde el modelo tiene un agujero invisible. Ver `HI_IRQ` en
    [09-historia-5g.md](09-historia-5g.md).
- **iPodLinux** — `ipodloader2/ipodhw.c`, de donde sale el bloque SysInfo que
  la ROM de arranque deja en lo alto de la IRAM.
- **ipodpatcher** — el modelo de arranque dual y la tabla `getmodel()`.
- **freemyipod.org** — wiki de ingeniería inversa de los iPod no-iOS.
  [Classic_6G](https://freemyipod.org/wiki/Classic_6G).
- **devos50/qemu-ios** — el emulador base, ramas `ipod_touch_1g` (S5L8900) y
  `ipod_touch_2g` (S5L8720).
- **Blogs de Martijn de Vos** — [Touch 1G](https://devos50.github.io/blog/2022/ipod-touch-qemu/),
  [Touch 2G](https://devos50.github.io/blog/2023/ipod-touch-2g-qemu/).

## Dónde está cada cosa

```
hw/arm/ipod_video/              modelos del PP502x — 8 ficheros
include/hw/arm/ipod_video/      sus cabeceras — 7 ficheros
tools/ipod_video/               herramientas del 5G

hw/arm/ipod_classic/            modelos del S5L8702
include/hw/arm/ipod_classic/    sus cabeceras
tools/ipod_classic/             herramientas del Classic

wiki/                           esta documentación
assets/                         GITIGNORADO — firmware, imágenes extraídas,
                                capturas, fuentes de referencia de Rockbox
```

`assets/` está fuera de git a propósito: es material que no es nuestro para
redistribuir, y hoy son **~1,2 GB**. Sin un volcado propio de un iPod no hay
nada que arrancar. Contiene:

| Ruta | Qué es |
|---|---|
| `assets/ipod-video-fw.bin` | volcado de la partición de firmware del 5G, **con el MBR delante** (82.252.800 B) |
| `assets/ipod-video-fwpart.bin` | el mismo **sin** los 63 sectores de cabecera — es el que quiere `fwtool.py` |
| `assets/ipod-video-disk.img` | imagen de disco construida con `mkdisk.sh` (MBR + firmware + FAT32) |
| `assets/images-5g/` | `osos_full.bin`, `apple_retailos.bin`, `rockbox_bootloader.bin`, `rockbox_app.bin`, `vmcs.bin` |
| `assets/apple/` | capturas del retailOS: arranque a 10–120 s y la secuencia de navegación |
| `assets/nav-baseline/` | capturas de referencia de Rockbox, para detectar regresiones |
| `assets/iPod_24.1.1.2.ipsw`, `assets/extracted/`, `assets/images/` | el firmware del Classic 6G, desempaquetado y extraído |
| `assets/ref/` | fuentes de Rockbox para S5L8702 e ipod6g |
| `assets/hello.bin` | binario de prueba generado por `mktestbin.py` |

## Herramientas propias

### `tools/ipod_video/` — el 5G

| Herramienta | Qué hace |
|---|---|
| `dump-firmware.ps1` | Vuelca la partición de firmware de un iPod conectado. **Windows, como administrador.** Abre el disco en solo lectura y se niega si hay más de un candidato o si es el disco de sistema |
| `mkdisk.sh` | Construye una imagen de disco arrancable: MBR + firmware + una FAT32 con `.rockbox`. Necesita `dosfstools` y `mtools` |
| `navtest.sh` | Prueba de navegación de **Rockbox** (regresión contra `assets/nav-baseline`) |
| `appletest.sh` | Prueba de navegación del **retailOS de Apple** |
| `run.sh` | Lanzador (`run.sh rockbox\|apple`). Pensado para trazar registros: **no engancha disco** |
| `extract-vc.py` | Saca `vmcs.bin` y el resto del FAT16 incrustado en la partición de firmware |
| `findlit.py` | Decodifica qué instrucciones ARM cargan de verdad un literal. Nació de un error de método: buscar bytes crudos daba 139 falsos positivos donde había 1 verdadero |

> `navtest.sh` y `appletest.sh` **no son aserciones**: salen con código 0 aunque
> QEMU muera al arrancar. Hay que mirar las cifras. Ver
> [05-build-notes.md](05-build-notes.md).

### `tools/ipod_classic/` — el Classic

| Herramienta | Qué hace |
|---|---|
| `fwtool.py` | Parsea y extrae las imágenes de un contenedor `]ih[`. **Se usa también para el 5G** |
| `mktestbin.py` | Genera binarios ARM de prueba |
| `tests/` | Tests bare-metal (timers, ruta de interrupción) |
