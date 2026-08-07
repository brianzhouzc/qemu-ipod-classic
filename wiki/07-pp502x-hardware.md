# PortalPlayer PP5020 / PP5022 — hardware

Fuente primaria: `firmware/export/pp5020.h` de Rockbox (476 defines) más
`firmware/target/arm/pp/` y `bootloader/ipod.c`. Copia local en
`assets/rockbox/`.

El chip del iPod que tenemos es un **PP5022C** (leído del silicio por Rockbox).
El PP5020 y el PP5022 comparten el mismo mapa de registros; Rockbox usa una
sola cabecera para ambos.

## Lo que lo hace distinto del S5L8702

**Dos núcleos ARM7TDMI.** Se llaman CPU y COP (coprocessor), corren el mismo
código desde la misma RAM y se distinguen leyendo un registro de identidad:

| Macro | Valor |
|---|---|
| `PROC_ID_CPU` | `0x55` |
| `PROC_ID_COP` | `0xaa` |

Se controlan con `CPU_CTL` (`0x60007000`) y `COP_CTL` (`0x60007004`). Al
arrancar, uno de los dos se queda dormido y el otro sigue. Es el riesgo
principal de este port: el S5L8702 era mononúcleo.

## Mapa de memoria

| Base | Bloque | Notas |
|---|---|---|
| `0x10000000` | **`DRAM_START`** | Coincide con el campo `addr` del directorio de firmware |
| `0x60004000` | Controlador de interrupciones | `CPU_INT_EN` en `+0x24`, banco alto en `+0x120` |
| `0x60005000` | Timers | `TIMER1_CFG` en `+0x00`, **`USEC_TIMER` en `+0x10`** |
| `0x60006000` | Reloj y alimentación | `DEV_EN` `+0x0c`, `CLOCK_SOURCE` `+0x20`, `PLL_CONTROL` `+0x34` |
| `0x60007000` | Control de núcleos | `CPU_CTL` `+0x00`, `COP_CTL` `+0x04` |
| `0x6000c000` | Caché | incluye `CACHE_CTL_VECT_REMAP` (`0x0010`) |
| `0x6000d000` | GPIO | `GPIOA_ENABLE` y siguientes |
| `0x70006000` | **UART serie 0** | layout **16550**: RBR/THR/IER/FCR/IIR/LCR/MCR/LSR/MSR/SPR/DLL |

> El UART es compatible 16550, así que QEMU ya lo tiene modelado
> (`hw/char/serial.c`). Es la primera pieza gratis del port.

### Números de IRQ conocidos

| IRQ | Fuente |
|---|---|
| 23 | `IDE_IRQ` (el disco) |
| 36 (`32+4`) | `SER0_IRQ` — el banco alto empieza en 32 |

## Cómo arranca un iPod 5G

De `bootloader/ipod.c`, que lo documenta con precisión:

> *The Apple ROM loader loads the entire OSOS image to DRAM_START, whatever it
> contains. If the bootloader is appended to another image then it will've
> modified the entry point in the OSOS header such that the ROM will call the
> bootloader rather than the main image.*

O sea:

1. La **ROM del PP** (en la flash NOR del chip, no en el disco) arranca al
   reset, lee la partición de firmware, carga **la imagen `osos` entera** en
   `DRAM_START`, y salta a `DRAM_START + entryOffset`.
2. Ese `entryOffset` sale del directorio de la partición de firmware.

**No necesitamos la ROM.** El emulador puede hacer su trabajo directamente:
cargar el `osos` en `0x10000000` y poner el PC donde toque.

### El arranque dual, byte a byte

`ipodpatcher` no sustituye el firmware de Apple: lo deja en el offset 0 de la
imagen `osos` y pega el bootloader de Rockbox detrás, en `entryOffset`.

```c
/* ipodpatcher.c, add_bootloader() */
if (ipod->ipod_directory[0].entryOffset > 0) {
    entryOffset = ipod->ipod_directory[0].entryOffset;
} else {
    entryOffset = (ipod->ipod_directory[0].len + ipod->sector_size-1)
                  & ~(ipod->sector_size-1);
}
memcpy(ipod->sectorbuf + entryOffset, ipod->bootloader, ipod->bootloader_len);
```

Para nuestro volcado (`entryOffset = 0x735a00`):

| Trozo | Rango en `osos` | Tamaño | Dirección virtual | Empieza con |
|---|---|---|---|---|
| **Apple retailOS** | `0x000000`–`0x735a00` | 7.559.680 B | `0x10000000` | tabla de vectores ARM |
| **Rockbox bootloader** | `0x735a00`–`0x742514` | 51.988 B | `0x10735a00` | `msr cpsr_c, #0xd3` |

Verificado: en `+0x20` del `osos` está la cadena `"portalplayer\0"`, que es
justo el marcador que `bootloader/ipod.c` busca en `DRAM_START+0x20` para
decidir si puede volver a saltar a `DRAM_START` y arrancar el firmware de
Apple.

Y la tabla de vectores del retailOS en `+0`:

```
+00  ea00007a   b +0x1f0      reset
+04  ea000067   b +0x1a4      undefined
+08  e59ff0dc   ldr pc,[pc]   SWI
+0c  ea00006b   b +0x1b4      prefetch abort
+10  ea000070   b +0x1c8      data abort
+14  eafffffe   b .           reservado
+18  ea000058   b +0x168      IRQ
+1c  ea00004e   b +0x140      FIQ
```

### Plan de arranque para el emulador

```
cargar assets/images-5g/osos_full.bin en 0x10000000
    PC = 0x10735a00   -> bootloader de Rockbox
    PC = 0x10000000   -> retailOS de Apple
```

Ambas imágenes están extraídas por separado en `assets/images-5g/` para poder
cargarlas sueltas mientras depuramos.

## Trampa: los offsets del directorio llevan un sesgo

Los offsets que guarda el directorio **no se cuentan desde el inicio de la
partición** sino desde el segundo sector: el primero aloja la cabecera `]ih[` y
todo lo que esta apunta se numera después de ella.

En la partición del iPod Video el sesgo es `+0x200`; en el `Firmware-XX` que
sale de un `.ipsw` del Classic 6G es `+0x1000`. `fwtool.py` deduce el sesgo
localizando el directorio y luego se lo aplica a todos los `devOffset`.

Cómo se detectó: `bootloader/ipod.c` dice que `"portalplayer"` está en
`DRAM_START+0x20`, y con los offsets sin corregir aparecía en `+0x220`.


## Dónde se queda el arranque (2026-08-06)

Con el remapeo MMAP y el suelo de registros, el firmware de Apple ejecuta
código real y extenso por todo su espacio: `0x82694`, `0x7d080`,
`0x144cc8`, `0x10c360`, `0x25f77c`, `0x90494`, `0xf23bc`, `0xe9b98`.

Se detiene en la instrucción de `0xe9b98`:

```
0x000e9b98:  ef123456      svc 0x123456
```

Una llamada al sistema. QEMU la vectoriza bien a `0x08`, donde el
`ldr pc,[pc,#0xdc]` de la tabla de vectores del propio firmware lleva a su
manejador. El manejador arranca y **hace push a una pila de modo supervisor
que nunca se inicializó**: la falta resultante es una escritura en
`0xffffffc8`, o sea SP envuelto desde cero.

Eso ya no es un hueco en el mapa de memoria. Es que el firmware llega a esa
ruta sin haber pasado por el código que configura las pilas de cada modo, o
bien las configura en algo que todavía no modelamos correctamente.

Siguiente paso: trazar hacia atrás desde `0xe9b98` para ver qué camino lleva
ahí, y comparar con la ruta de inicialización que el firmware debería haber
recorrido antes.
