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

Todo sale de `pp5020.h` y `ata-target.h` salvo donde se indique. La columna
"modelo" dice qué hay en `hw/arm/ipod_video/`.

| Base | Bloque | Modelo |
|---|---|---|
| `0x00000000` | flash NOR mientras arranca; después DRAM (remapeada por MMAP0) | ventana en blanco con la revisión de HW en `+0x405c` |
| `0x10000000` | **`DRAM_START`**, 64 MB. Coincide con el `addr` del directorio de firmware | RAM |
| `0x20000000` | la misma NOR ya remapeada (`hwcompat.h` lee la revisión en `0x2000405c`) | alias |
| `0x30000000` | **Broadcom BCM2722**, el que manda al panel | `ipod_video_bcm.c` |
| `0x40000000` | IRAM | RAM |
| `0x60000000` | `PROCESSOR_ID` | sí |
| `0x60001000` | Buzón CPU-COP (`MBX_MSG_STAT/SET/CLR` en `+0`/`+4`/`+8`) | **no**, cae en el suelo de `-d unimp` |
| `0x60004000` | Controlador de interrupciones. Banco bajo en `+0x000`, **banco alto en `+0x100`** | `ipod_video_intc.c` |
| `0x60005000` | Timers. `TIMER1_CFG` `+0x00`, **`USEC_TIMER` `+0x10`** | sí |
| `0x60006000` | Reloj y alimentación. `DEV_RS` `+0x04`, `DEV_EN` `+0x0c`, `CLOCK_SOURCE` `+0x20`, `PLL_CONTROL` `+0x34`, **`PLL_STATUS` `+0x3c`** | sí |
| `0x60007000` | Control de núcleos. `CPU_CTL` `+0x00`, `COP_CTL` `+0x04` | parcial: el CPU no duerme de verdad |
| `0x6000a000` | DMA genérico | no |
| `0x6000c000` | Caché. `CACHE_CTL` con `VECT_REMAP` (`0x0010`) y `READY` (`0x4000`) | sí |
| `0x6000d000` | GPIO. Por grupo: `ENABLE` `+0x00`, `OUTPUT_EN` `+0x10`, `OUTPUT_VAL` `+0x20`, `INPUT_VAL` `+0x30` | sí |
| `0x70000000` | **Control de dispositivo**. `PP_VER1` `+0x00`, `DEV_INIT1` `+0x10`, `DEV_INIT2` `+0x20`, **`+0x28` sin nombre en `pp5020.h`**, `DEV_TIMING1` `+0x34`, `GPO32_VAL` `+0x80`, `GPO32_ENABLE` `+0x84` | sí |
| `0x70002800` | I2S | no |
| `0x70003000` | LCD | no, el panel va por el Broadcom |
| `0x70006000` | **UART serie 0**, 16550 con stride de 4 | `hw/char/serial.c` de QEMU |
| `0x7000c000` | I2C **y** click wheel comparten ventana: I2C en `+0x00`, rueda en `+0x100`, `+0x104` y `+0x140` | sí |
| `0xc3000000` | **IDE**. Temporizaciones `+0x00`, `IDE0_CFG` `+0x28`, task file `+0x1e0`, control `+0x3f8`, DMA `+0x400` | `ipod_video_ide.c` |
| `0xc5000000` | USB | no |
| `0xf0000000` | **Arrays de la caché**: datos `+0`, espejo `+0x2000`, estado CPU `+0x4000`, estado COP `+0x6000`, flush `+0x8000`, invalidate `+0xc000` | RAM |
| `0xf000f000` | Unidad de remapeo `MMAP0..7` (pares lógico/físico) más `CACHE_MASK` `+0x40` | sí |

> El UART es 16550 de verdad, así que QEMU ya lo tenía. Fue la primera pieza
> gratis del port.

### Números de IRQ

Banco bajo (0..31) y banco alto (32..63); el alto se encadena por `HI_IRQ`.

| IRQ | Fuente |
|---|---|
| 0, 1 | `TIMER1_IRQ`, `TIMER2_IRQ` |
| 4 | `MAILBOX_IRQ` |
| 10 | `IIS_IRQ` |
| 20 | `USB_IRQ` |
| **23** | `IDE_IRQ`, el disco |
| 25, 26 | `FIREWIRE_IRQ`, `DMA_IRQ` |
| 30 | `HI_IRQ`, encadena el banco alto |
| 32..34 | `GPIO0/1/2_IRQ` |
| 36, 37 | `SER0_IRQ`, `SER1_IRQ` |
| **40** | `I2C_IRQ`, **y también el click wheel** |

## Registros con comportamiento

La mayoría de los registros de este SoC se pueden almacenar y devolver tal
cual. Los que no, son exactamente los que bloquean el arranque, y todos fallan
igual: el emulador parece vivo, no salta ninguna excepción, y el firmware está
esperando un bit que nunca llega. La historia de cómo se encontró cada uno
está en [04-roadmap.md](04-roadmap.md).

| Registro | Bit | Qué significa | Quién lo espera |
|---|---|---|---|
| `PLL_STATUS` `0x6000603c` | 31 | PLL enganchado | `system-pp502x.c`: `while (!(PLL_STATUS & 0x80000000));` |
| `0x70000028` | 7 | el bloque USB salió de reset | `usb-fw-pp502x.c`: `while ((inl(0x70000028) & 0x80) == 0);` |
| `GPO32_VAL` `0x70000080` | 14 | el Broadcom está alimentado | `lcd-video.c`. Si está a 0 intenta subir el firmware `vmcs` de la NOR, se rinde, deja `display_on = false`, y **`lcd_update_rect()` descarta todos los dibujados en silencio** |
| `IDE0_CFG` `0xc3000028` | 3 | `INTRQ`, transferencia terminada | `ata-pp5020.c`. Es un **pestillo**, no un espejo de la línea, y se borra escribiendo un uno |
| `0x7000c104` | 26 | hay paquete de rueda que leer | `button-clickwheel.c`. Sin él `ipod_4g_button_read()` ni mira los datos |
| `CACHE_CTL` `0x6000c000` | 14 | `READY` | se devuelve siempre puesto: no hay caché que esperar |

Y dos zonas que no son registros sino memoria direccionable, y que hay que
respaldar con RAM o el primer acceso se rechaza:

- **`0xf0000000`-`0xf000efff`**, los arrays de la caché.
  `cache_invalidate_special()` recorre a mano todo el array de estado marcando
  cada línea como válida, como remedio a un fallo del silicio.
- **`0x60000000` y `0x70000000` enteros**. El firmware de Apple toca registros
  que no aparecen en `pp5020.h` (el primero fue `0x60009000`). Un registro
  desconocido que devuelve cero es sobrevivible; uno sin mapear es un data
  abort, y con la tabla de vectores aún sin instalar el CPU no vuelve. Por eso
  hay un `create_unimplemented_device` cubriendo ambas ventanas por debajo.

## El IDE y su DMA

El task file es ATA estándar con los registros separados cuatro bytes, así que
lo lleva el núcleo IDE de QEMU. Lo propio del PP es lo de debajo:

| Registro | Dirección | Función |
|---|---|---|
| `IDE0_PRI_TIMING0/1` | `0xc3000000` y `0xc3000004` | temporizaciones PIO y DMA |
| `IDE0_CFG` | `0xc3000028` | bit 3 `INTRQ`, bit 15 activo durante el DMA |
| `IDE_DMA_CONTROL` | `0xc3000400` | bit 0 arranca, bit 1 arma, bit 3 dirección (1 = a memoria) |
| `IDE_DMA_LENGTH` | `0xc3000408` | bytes **menos 4** |
| `IDE_DMA_ADDR` | `0xc300040c` | dirección física destino |

No es bus-master PCI: es un buffer plano. La dirección y la longitud se
programan **antes** del comando ATA y el bit de arranque **después**, así que
`start_dma` solo guarda el callback del núcleo y la escritura del bit 0 es la
que dispara la transferencia.

El bootloader de Rockbox lee con `READ MULTIPLE` (PIO) y la aplicación con
`READ DMA`, así que sin este motor el bootloader funciona y la app no.

## El click wheel

Comparte ventana con el I2C en `0x7000c000` y **comparte su interrupción**
(`I2C_IRQ`, la 40). Va por interrupción, no por sondeo:
`ipod_4g_button_read()` solo se llama desde `ipod_4g_button_int()`.

| Dirección | Función |
|---|---|
| `0x7000c100` | configuración; `opto_i2c_init` escribe `0xc00a1f00` |
| `0x7000c104` | estado. Bit 26 = hay paquete. Se reconoce escribiendo `0x0c000000` encima |
| `0x7000c140` | `CLICKWHEEL_DATA`, el paquete |

Formato del paquete, de `button-clickwheel.c`:

```
paquete valido:  (status & 0x800000ff) == 0x8000001a
  bit 8   SELECT     bit 9   RIGHT     bit 10  LEFT
  bit 11  PLAY       bit 12  MENU
  bit 30  rueda tocada
  bits 16..22  posicion, 0..0x5F, el sentido horario incrementa
```

96 clics por vuelta (`WHEELCLICKS_PER_ROTATION`). Rockbox exige
`WHEEL_SENSITIVITY` (4) clics acumulados antes de mover la selección un ítem, y
**reinicia el acumulador al cambiar de sentido**, así que la primera pulsación
después de invertir la dirección no mueve nada. Eso es del firmware, no del
modelo.

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


## La otra vía: el retailOS de Apple

Todo lo anterior es la vía de Rockbox, que ya arranca hasta el menú (ver
[04-roadmap.md](04-roadmap.md)). El firmware original de Apple es una vía
aparte, y ahora sabemos exactamente por qué no arranca.

### El síntoma

Con `-M iPod-Video,entry=0` la pantalla se queda negra (0 píxeles encendidos) y
solo se traducen 266 bloques, frente a ~3700 de Rockbox. La secuencia de
excepciones es:

```
Taking exception 2 [SVC]
Taking exception 1 [Undefined Instruction]
Invalid write at addr 0xFFFFFFC8 ... rejected
Taking exception 4 [Data Abort]        <- en bucle infinito
```

Eso es el **final** de la historia, no el principio. Trazando hacia atrás:

### La cadena, de atrás hacia delante

El vector SWI del propio retailOS, en la tabla de vectores de la imagen:

```
+08: e59ff0dc   ldr pc, [pc, #0xdc]   -> literal en 0xEC = 0x40000008
```

O sea que su manejador de SWI vive en **IRAM**, que en nuestra máquina está
vacía. Al hacer el SVC, el CPU salta a IRAM en blanco, camina hacia delante
por los ceros hasta tropezar con una instrucción indefinida, y el manejador de
esa excepción hace `push` sobre una pila que nunca se montó: `0xFFFFFFC8` es el
puntero de pila envuelto por debajo de cero. De ahí el bucle.

Pero el SVC no es un fallo, es **deliberado**: `0xe9b98` es el reportador de
fallos del firmware. Cerca están las cadenas `"Type: "` y `"Undefined
instruction"`, y el propio SVC es `ef123456`, un valor mágico. El firmware ya
había detectado un problema por software y estaba intentando informarlo.

¿Qué problema? La llamada viene de aquí:

```
144cc8: movs r3, r0        ; el resultado es cero?
144cd4: beq  0x10c360      ; si -> reportar fallo
```

y quien la invoca es un assert con número de línea:

```
d98: ldr r0, [r4, #0x24]   ; fin calculado de la imagen
d9c: ldr r1, [r4, #0x20]   ; memoria disponible
da0: cmp r0, r1
da8: movhi r1, #231        ; numero de linea
db0: blhi 0x144cc8         ; si no cabe -> fallo
```

Leyendo la estructura en ejecución con `xp /16xw 0x40006000`:

```
+0x10 = 0x10000000   DRAM_START
+0x14 = 0x00800000   8 MB
+0x18 = 0x0020e294   tamaño de la imagen
+0x20 = 0x00000000   <- memoria disponible: CERO
+0x24 = 0x00a0e294   fin calculado, ~10 MB
```

`0xa0e294 > 0`, así que salta. **El firmware cree que el aparato tiene cero
bytes de RAM.**

### De dónde sale ese cero

`+0x20` es la suma de cuatro búsquedas en una tabla, indexadas por los cuatro
nibbles de un valor empaquetado (`0xdf8`). La tabla está en `0x16d8` y es de
tamaños potencia de dos:

| índice | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| tamaño | 0 | 512K | 1M | 2M | 4M | 8M | 16M | 32M | **64M** | 128M |

Es la configuración de bancos de SDRAM: un nibble por banco. El valor
empaquetado arranca como una constante, `mov fp, #0x700` en `0x1f4`, pero antes
de usarlo el nibble 2 se sobrescribe (`0xe6c` es un setter de nibble) con un
campo de una estructura que resulta valer cero.

Y esa estructura nunca se rellena, porque quien la rellena es esto:

```
95024: ...
9503c: bl  0xb68
95040: cmp r0, #0
95044: beq 0x95064      ; si r0 == 0, salir sin hacer nada

b68:   mov  r0, #0x60000000   ; PROCESSOR_ID
b6c:   ldrb r0, [r0]
b70:   cmp  r0, #0xaa         ; soy el COP?
b74:   movne r0, #0           ; no -> 0
b78:   moveq r0, #1
```

### La causa raíz: el retailOS necesita los dos núcleos

`0x95024` **solo hace su trabajo si corre en el COP**. En el CPU sale
inmediatamente sin tocar nada.

Y no es un caso aislado. La misma imagen corre en los dos núcleos y se bifurca
nada más arrancar:

```
240: mov  r0, #0x60000000
244: ldrb r2, [r0]
248: cmp  r2, #0x55        ; soy el CPU?
24c: beq  0x25c            ; si  -> pila del CPU, camino del CPU
250: ldr  sp, [pc, #168]   ; no -> pila del COP
254: bl   0x1498           ;        init del COP
```

Nuestra máquina tiene `mc->max_cpus = 1`, un solo ARM, y
`pp502x_proc_id_read()` devuelve siempre `PP_PROC_ID_CPU`. El COP no existe, así
que nunca ejecuta su mitad de la inicialización, y el CPU acaba leyendo ceros
donde esperaba la configuración de memoria.

**Y esto es justo lo que distingue a Rockbox del retailOS.** Rockbox también
espera al COP —`core_thread_init()` lo despierta y se duerme esperándolo— pero
como nuestro `sleep_core` no duerme de verdad, el CPU sigue adelante y arranca
igual, sacrificando solo lo que Rockbox hubiera programado en el segundo
núcleo. El retailOS no es tan indulgente: sin el COP no calcula ni el tamaño de
la RAM.

### Lo que haría falta

Modelar el segundo núcleo de verdad:

- Un segundo ARM en la máquina, `max_cpus = 2`.
- `PROCESSOR_ID` (`0x60000000`) devolviendo `0x55` o `0xaa` **según qué núcleo
  lee**, no una constante.
- `CPU_CTL` / `COP_CTL` (`0x60007000` / `+0x04`) con dormir y despertar reales.
- El buzón de `0x60001000` (`MBX_MSG_STAT` / `SET` / `CLR`), hoy sin modelar.
- Los bancos por núcleo del controlador de interrupciones: los registros
  `COP_INT_EN` y compañía ya existen en el mapa, pero el modelo solo enruta al
  CPU.

### Lo que NO hace falta, y me equivoqué al suponerlo

Antes de trazar esto planteé como hipótesis que el problema fuese la **flash
ROM en blanco**, porque el retailOS es el firmware nativo de esa ROM y nosotros
le damos 1 MB de ceros con una sola palabra dentro. Esa hipótesis **no se
sostiene**: el firmware no llega ni a mirar la flash antes de morir. El
bloqueo es el segundo núcleo, y se puede atacar sin pedirle ningún volcado al
usuario.

La flash seguirá haciendo falta más adelante —`lcd-video.c` saca de ella el
firmware del Broadcom, sección `vmcs`— pero no es el obstáculo de ahora.
