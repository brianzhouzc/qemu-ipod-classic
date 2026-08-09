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

## Estado

Este aparato es el que arranca. Con su propio volcado del `osos` y una imagen
de disco construida con `tools/ipod_video/mkdisk.sh`, el emulador llega hasta
el menú de Rockbox y se navega con la rueda. Ver [04-roadmap.md](04-roadmap.md)
para el recorrido y [07-pp502x-hardware.md](07-pp502x-hardware.md) para los
registros.

Resuelto desde que se escribió esta página:

- **Dónde acaba el bootloader de Rockbox y empieza el firmware de Apple dentro
  del `osos`**: está en la tabla de 07, byte a byte. El retailOS ocupa
  `0x000000`-`0x735a00` y el bootloader va detrás, en `entryOffset`.
- **El mapa de registros del PP5022C**: en 07, con qué está modelado y qué no.

## Volcar la flash ROM del aparato

No hace falta compilar nada: **Rockbox ya lo trae**. La entrada existe desde
antes de la v3.0, así que cualquier build de `ipodvideo` sirve.

`apps/debug_menu.c`:

```c
#elif defined(CPU_PP) && !(CONFIG_STORAGE & STORAGE_SD)
static bool dbg_save_roms(void)
{
    int fd = creat("/internal_rom_000000-0FFFFF.bin", 0666);
    if(fd >= 0) {
        write(fd, (void *)0x20000000, FLASH_SIZE);
        close(fd);
    }
    return false;
}
```

Aplica a este aparato: `ipodvideo.h` define `CONFIG_STORAGE STORAGE_ATA`
(no SD), `CONFIG_CPU PP5022` (o sea `CPU_PP`) y `FLASH_SIZE 0x100000`. El mod
iFlash no lo rompe, porque `CONFIG_STORAGE` es una constante de compilación, no
depende del medio físico.

### Procedimiento

1. Expulsa el iPod del PC y arranca **Rockbox** (no el firmware de Apple).
2. MENU → **System** → **Debug (Keep Out!)**.
3. El **primer** elemento de la lista es **`Dump ROM contents`**. Selecciónalo.
4. **No avisa de nada**: ni confirmación, ni barra, ni splash. Espera un par de
   segundos y sal con LEFT.
5. Conecta el USB para que monte la FAT32.
6. En la **raíz** aparece `internal_rom_000000-0FFFFF.bin`, de exactamente
   **1.048.576 bytes**.

Avisos, todos verificados en el código: no comprueba el retorno de `write()`
ni si queda espacio, y si `creat()` falla se calla y no pasa nada. Si el
fichero no aparece, es por eso. Se escribe siempre con el mismo nombre en `/`,
así que reejecutarlo sobrescribe.

**Comprobación de que el volcado es bueno**: en el offset `0xFFE00` deben verse
los bytes ASCII `hslf` (que es `flsh` en little-endian). Si está todo a ceros,
ha fallado.

### Formato de la ROM

Directorio en `0xFFE00` (los últimos 512 bytes del MB), entradas de `0x28`
bytes, terminadas cuando el magic deja de ser `flsh`:

| Offset | Campo |
|---|---|
| `+0x00` | magic `flsh` |
| `+0x04` | id de imagen: `disk`, `diag`, `logo`, `scan`, **`vmcs`** |
| `+0x0C` | offset dentro de la flash |
| `+0x10` | longitud |
| `+0x14` | dirección de carga |
| `+0x1C` | checksum (suma simple de bytes) |
| `+0x20` | versión; byte alto `0xb0` = iPod Video |

Lo confirman dos fuentes independientes que coinciden: `flash_get_section()` en
`firmware/target/arm/ipod/video/lcd-video.c` (que lee la ROM en vivo desde
`0x20000000`) y `utils/ipod/flashsplit/flashsplit.c` (que trocea un volcado).
`flashsplit` produce `diskmode`, `diagmode`, `logo`, `diskscan` y `vmcs`, y debe
imprimir `ipod model: ipvd`.

### Para qué lo queremos

**No para el bloqueo actual.** El retailOS muere antes de mirar la flash (ver
[07-pp502x-hardware.md](07-pp502x-hardware.md)); el obstáculo es el segundo
núcleo. Pero la sección **`vmcs`** es el firmware del Broadcom, que es lo que
`lcd-video.c` busca cuando el BCM no viene ya encendido — hoy lo esquivamos
poniendo `GPO32_VAL` bit 14 a mano. Con el volcado real podríamos modelar esa
ruta en vez de cortocircuitarla, y tendríamos además `diskmode` y `diagmode`,
que son imágenes arrancables independientes y objetivos mucho más pequeños que
el retailOS entero.

Lo que **no** vale: `ipodpatcher` solo lee la partición de firmware del disco,
no la NOR. El `getflash` de iPodLinux decodifica el actualizador `aupd` que
Apple deja en el disco, que es la imagen que Apple *escribiría*, no una lectura
del chip (y su SVN está muerto).

## Pendiente

- **Los dos núcleos.** `core_thread_init()` despierta al COP y se duerme
  esperándolo. Como nuestro `sleep_core` no duerme de verdad, el CPU sigue y
  arranca igual, pero el COP nunca ejecuta nada, así que lo que Rockbox le
  programe se queda sin hacer. El buzón de `0x60001000` tampoco está modelado.
- **Audio.** Ni I2S ni códec: no suena.
- **El firmware de Apple**, que es una vía aparte y sigue atascada en una
  llamada al sistema con la pila de supervisor sin inicializar.
