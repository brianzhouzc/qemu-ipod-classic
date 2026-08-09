# Emulación de iPods clickwheel en QEMU

Bitácora de ingeniería inversa y desarrollo del port de QEMU-iOS. Rama de
trabajo: `ipod_classic_6g`, partiendo de `ipod_touch_2g` (QEMU 8.2.0).

Empezó apuntando al **iPod classic 6G** (S5L8702) y se topó con que su firmware
está cifrado con una clave que vive en el silicio. Al aparecer un **iPod Video
5G** físico, cuyo firmware está en claro, el objetivo de arrancar software real
se movió allí.

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Video` | PP5022C | **arranca Rockbox hasta el menú y se navega con la rueda** |
| `iPod-Classic` | S5L8702 | ejecuta código; timers e interrupciones verificados |

Para ejecutarlo y probarlo: [05-build-notes.md](05-build-notes.md).

## Índice

| Documento | Contenido |
|---|---|
| [01-firmware-ipsw.md](01-firmware-ipsw.md) | Formato del `.ipsw`, la partición de firmware `]ih[` y la cabecera de imagen `8702`. Qué contiene el firmware que tenemos. |
| [02-s5l8702-hardware.md](02-s5l8702-hardware.md) | Mapa de memoria, periféricos, números de IRQ y componentes de placa del S5L8702 / iPod classic 6G. |
| [03-reuse-qemu-ios.md](03-reuse-qemu-ios.md) | Qué modelos de dispositivo de la rama `ipod_touch_2g` se pueden reaprovechar y cuáles hay que escribir de cero. |
| [04-roadmap.md](04-roadmap.md) | Plan por hitos, estado actual, y el recorrido de cada bloqueo que hubo que quitar para llegar al menú. |
| [05-build-notes.md](05-build-notes.md) | Cómo compilar (WSL), **cómo ejecutarlo y probarlo**, teclas del click wheel, y cómo averiguar dónde se ha quedado el firmware. |
| [07-pp502x-hardware.md](07-pp502x-hardware.md) | **Referencia del PP5020/PP5022**: mapa de memoria con qué está modelado, IRQs, los registros que tienen comportamiento de verdad, el IDE con su DMA, el protocolo del click wheel y el modelo de arranque. |
| [06-ipod-video-5g.md](06-ipod-video-5g.md) | El iPod Video (PP5022C) que sí tenemos físicamente: identificación, layout del disco, su firmware **sin cifrar**, y qué falta. |
| [08-s5l8702-fuentes-externas.md](08-s5l8702-fuentes-externas.md) | Correcciones al mapa de memoria, bootrom, cadena de arranque, click wheel, y el **modelo QEMU del S5L8702 que ya existe** en `freemyipod/qemu`. |

## Fuentes

- **Rockbox** — port completo y funcional al iPod classic 6G, con drivers
  open source a nivel de registro. Es la fuente primaria del mapa de memoria.
  - `firmware/export/s5l87xx.h` (2052 líneas, ~1400 defines) — el mapa de registros.
  - `firmware/target/arm/s5l8702/` y `firmware/target/arm/s5l8702/ipod6g/`.
  - `bootloader/ipod-s5l87xx.c`.
  - Copia local de todo esto en `assets/ref/`.
- **freemyipod.org** — wiki de ingeniería inversa de los iPod no-iOS.
  [Classic_6G](https://freemyipod.org/wiki/Classic_6G).
- **devos50/qemu-ios** — el emulador base, ramas `ipod_touch_1g` (S5L8900) y
  `ipod_touch_2g` (S5L8720).
- **Blogs de Martijn de Vos** — [Touch 1G](https://devos50.github.io/blog/2022/ipod-touch-qemu/),
  [Touch 2G](https://devos50.github.io/blog/2023/ipod-touch-2g-qemu/).

## Dónde está cada cosa

Todo el trabajo del iPod classic vive dentro de este repo:

```
hw/arm/ipod_classic/            modelos de dispositivo del S5L8702
include/hw/arm/ipod_classic/    sus cabeceras
tools/ipod_classic/             herramientas propias (firmware, binarios de prueba)
wiki/                           esta documentación
assets/                         GITIGNORADO — firmware, imágenes extraídas,
                                fuentes de referencia de Rockbox, binarios de prueba
```

`assets/` está fuera de git a propósito: son ~170 MB de material que no es
nuestro para redistribuir. Contiene:

| Ruta | Qué es |
|---|---|
| `assets/iPod_24.1.1.2.ipsw` | el firmware original |
| `assets/extracted/` | el ZIP del `.ipsw` desempaquetado |
| `assets/images/` | `osos` / `aupd` / `rsrc` extraídos con `fwtool.py` |
| `assets/ref/` | fuentes de Rockbox para S5L8702 e ipod6g |
| `assets/hello.bin` | binario de prueba generado por `mktestbin.py` |

## Herramientas propias

- `tools/ipod_classic/fwtool.py` — parsea y extrae las imágenes de un
  `Firmware-XX` de iPod classic.
