# iPod Video (5G/5.5G) — el aparato real

Hardware disponible físicamente, volcado el 2026-08-06. A diferencia del
Classic 6G, **su firmware no está cifrado**, lo que lo convierte en el único
camino realista hacia arrancar el sistema operativo original de Apple.

## Identificación

Fuentes contradictorias, resueltas por orden de fiabilidad:

| Dato | Valor | Fuente |
|---|---|---|
| Número de modelo | `MA146E` | menú *Ajustes → Acerca de* de Apple |
| **Chip** | **PP5022C** | Rockbox, leído del registro de versión del propio SoC |
| Revisión hardware | `0x000b0005` | Rockbox HW info |
| Reloj | 81805 (~81,8 MHz) | Rockbox HW info |
| RAM | 64 MB | `rockbox-info.txt` y HW info |
| `vers` del `osos` | `0x0000b012` → `>>8` = **`0xb0`** | el propio firmware |
| USB | `VID_05AC` / `PID_1209` | Windows |

`0xb0` es exactamente lo que la tabla `getmodel()` de `ipodpatcher` etiqueta
como *"Video (aka 5th Generation)"*, target `ipodvideo`. Ese dato sale del
firmware, así que es el que manda.

El número de modelo `MA146` aparece en las bases de datos como un 5G de 30 GB
(que llevaría 32 MB de RAM y un PP5021C), lo cual **no cuadra** con los 64 MB y
el PP5022C que reporta el silicio. El aparato está modificado: lleva un
adaptador **iFlash** con una tarjeta de 8 GB en lugar del disco duro original
(el modelo del disco se reporta como `iFlash-Platform iPod Ada`). Lo más
probable es que se hayan mezclado piezas. **Para el emulador manda el silicio:
PP5022C con 64 MB.**

## Layout del disco

```
Sector 0        cabecera con pinta de VBR de FAT (EB 58 90 "MSDOS5.0")
                + tabla de particiones MBR valida en 0x1BE, firma 55AA

Particion #0    tipo 0x00   LBA 63       160587 sectores   <- FIRMWARE
Particion #1    tipo 0x0B   LBA 160650   15117429 sectores <- FAT32 "IPOD" (D:)
```

Windows **no muestra la partición de firmware** porque su tipo es `0x00`. Hay
que leerla en crudo: empieza en `63 × 512 = 0x7E00` y ocupa `160587 × 512 =
82.220.544` bytes.

`tools/ipod_video/dump-firmware.ps1` automatiza el volcado (requiere
administrador; abre el disco en solo lectura).

## Contenido de la partición de firmware

Mismo contenedor `]ih[` que el Classic 6G. Directorio en `0x4200`:

| # | tipo | offset | tamaño | addr | entry | vers |
|---|---|---|---|---|---|---|
| 0 | `osos` | `0x004400` | `0x742514` (7,6 MB) | `0x10000000` | `0x735a00` | `0x0000b012` |
| 1 | `rsrc` | `0x746c00` | `0x500000` (5 MB) | `0x10000000` | — | `0x0000b012` |
| 2 | `aupd` | `0xc46e00` | `0x106400` (1 MB) | `0x10000000` | — | `0x0000b012` |
| 3 | `hibe` | `0xd4d600` | `0x2000800` (33 MB) | `0x10000000` | — | 0 |

> **`addr = 0x10000000`** es la dirección de carga en el PP502x — el ancla del
> mapa de memoria, igual que `0x08000000` lo era para el S5L8702.

### Trampa: el offset del directorio miente

La cabecera dice `dir_offset = 0x4000` en `+0x104`, pero el directorio real
está en `0x4200`. Y en el `Firmware-XX` que sale de un `.ipsw` del Classic 6G,
el mismo campo `0x4000` apunta a `0x5000`. El desplazamiento cambia según de
dónde venga el contenedor.

`fwtool.py` ya no se fía del campo: prueba los desplazamientos conocidos
(`+0x1000`, `+0x200`, `+0`) y, si ninguno cuela, escanea buscando lo primero
que parsee como entrada de directorio válida. Valida que ambos códigos de
cuatro caracteres sean imprimibles y que el offset caiga dentro del fichero,
que es lo que evita leer 81 entradas de basura.

## Sin cifrar — verificado

| Medida | iPod Video | Classic 6G |
|---|---|---|
| Entropía primeros 64K | 6,348 bits/byte | 7,937 |
| Entropía en mitad | 5,228 | 7,997 |
| Entropía al final | 5,650 | 7,998 |
| Cadenas ASCII ≥6 | 21.016 | ~0 (ruido) |

Y las cadenas son inequívocas:

```
portalplayer
PP5020AF-05.00-PP07-05.00-PP07-05.00-DT
2003.10.30  (Build 04)
Digital Media Platform
Copyright(c) 1999 - 2003 PortalPlayer, Inc.  All rights reserved.
booting!  hotstuff  battislo  diskmode  diskscan  softupdt  retailOS
```

`retailOS`, `diskmode`, `diskscan`, `softupdt` son los nombres de imagen del
cargador de PortalPlayer. **El sistema operativo de Apple está ahí, legible.**

## Rockbox ya instalado

El iPod tiene arranque dual. `ipodpatcher` mete el bootloader de Rockbox dentro
de la imagen `osos` y conserva el firmware de Apple detrás; al arrancar se
elige uno u otro.

`D:\.rockbox\rockbox-info.txt`:

```
Target: ipodvideo
Target id: 15
Target define: -DIPOD_VIDEO
Memory: 64
Binary: rockbox.ipod   (765676 bytes)
gcc: arm-elf-eabi-gcc (GCC) 4.9.4
```

Eso nos da tres binarios conocidos y funcionando contra los que probar el
emulador: el bootloader de Rockbox, `rockbox.ipod`, y el `retailOS` de Apple.

## Pendiente

- Separar dentro del `osos` dónde acaba el bootloader de Rockbox y dónde
  empieza el firmware original de Apple.
- Mapa de registros del PP5022C (iPodLinux y `firmware/target/arm/pp/` de
  Rockbox).
- Los dos núcleos ARM7TDMI (CPU + COP) y su mecanismo de mailbox.
