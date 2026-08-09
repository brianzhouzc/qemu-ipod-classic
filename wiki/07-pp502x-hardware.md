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
| `0x7000c000` | I2C **y** click wheel comparten ventana: I2C en `+0x00`, rueda en `+0x100`, `+0x104` y `+0x140` | `ipod_video_i2c.c` + `ipod_video_pcf50605.c`, y la rueda en `ipod_video.c` |
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
| `I2C_STATUS` `0x7000c01c` | 6 y 2:0 | `BUSY` y el código de resultado | el driver de Apple (`0x282f44`) devuelve `STATUS & 7` como error y aborta si no es cero |
| `ADCS2` del PCF50605 (reg `0x31`) | 7 | `ADCRDY`, conversión terminada | `pmu_adc_read` (`0x28341c`) gira sobre él **sin timeout** |
| `BCM_ALT_CONTROL` `0x30070000` | 7 y 6 | ocupado / listo, el arranque del Broadcom | `bcm_init()` de Rockbox y `0x2876d0`/`0x2876dc` de Apple |

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

## El I²C y el PCF50605

El bloque está en `0x7000c000`, debajo del click wheel. **No es un
bit-banger**: se le da una dirección de esclavo, hasta cuatro bytes de datos y
una longitud, y un bit de arranque ejecuta la transferencia entera. Los dos
firmwares lo manejan con los mismos cinco pasos en el mismo orden —`i2c-pp.c`
de Rockbox y el driver de Apple en `0x282d90` (lectura) y `0x282f58`
(escritura).

| Registro | Offset | Ancho | Bits |
|---|---|---|---|
| `CTRL` | `+0x00` | byte | bit 7 `SEND` (arranca, y **se autoborra**), bit 5 = lectura, bits 2:1 = longitud−1 |
| `ADDR` | `+0x04` | byte | primer byte del bus: `(dir7 << 1) \| R/W` |
| `ADDR2` | `+0x08` | byte | **solo Apple**: dirección de la fase de lectura de una transacción combinada |
| `DATA0..3` | `+0x0c`…`+0x18` | byte | |
| `STATUS` | `+0x1c` | byte | bit 6 `BUSY`; **bits 2:0 = código de resultado** |

Tres cosas que solo enseña el firmware de Apple, y las tres importan:

- **`STATUS[2:0]` es un código de error.** `0x282f44` hace `and r0, r0, #7` y
  `0x28358c` aborta la transacción si no es cero. Rockbox ni lo mira. El
  fichero de registros plano que había antes lo cumplía por accidente, que es
  por lo que nadie notó que no había controlador.
- **`+0x08` es una segunda dirección**, para la escritura-más-lectura combinada
  de `0x282e30` (`CTRL = 0x48 | SEND`). El byte recibido aparece en **`DATA1`,
  no en `DATA0`**: los registros de datos se consumen en orden, primero los
  transmitidos.
- **El bloque tiene que funcionar sin inicializar.** La rutina de
  `DEV_EN`/`DEV_RS`/`0x600060a4` de Apple (`0x282e98`) solo se alcanza desde
  `0x282f0c`, que la llama cuando una transferencia expira. Si nuestro `STATUS`
  nunca dice `BUSY`, ese camino no se ejecuta jamás.

**Ninguno de los dos usa la interrupción**; los dos sondean. Y en este SoC la
línea que la cabecera llama `I2C_IRQ` es la del click wheel, así que el modelo
no la levanta.

**Sin determinar**: qué significan por separado los bits 6 y 3 de `CTRL` (solo
se ven juntos, como `0x48`), qué son los códigos 1..6 de `STATUS[2:0]`, y qué
chip vive en la dirección `0x2a`, con la que Apple habla (`0x0e4164`,
`0x0e9410`) y que no nombra ninguna fuente pública. El códec de audio está en
`0x1a` según Rockbox y **no hay ni un acceso a esa dirección en la imagen de
Apple**.

### El esclavo: PCF50605 en la dirección de 7 bits `0x08`

Es un dispositivo de puntero de registro: una escritura empieza por el número
de registro y el puntero avanza tras cada byte; una lectura sigue desde donde
quedó. **Que el puntero sobreviva al final de una transacción no es una
suposición**: los dos drivers dependen de ello. Una lectura de los siete
registros del RTC es una escritura y dos lecturas, con el puntero corriendo a
través de las tres.

Modelado: el fichero de registros, el conversor A/D, y el reloj (desde el reloj
del anfitrión, por la opción `-rtc` de QEMU). Sin modelar: la carga, el
controlador de encendido (un guest que escriba `GOSTDBY` se ignora en vez de
apagar la máquina), los reguladores, la alarma del RTC y las interrupciones.

El ADC: `ADCC2` (`0x2f`) bit 0 arranca la conversión y se autoborra, bits 4:1
eligen la entrada, bit 7 pide 8 bits. El resultado son diez bits repartidos:
los ocho altos en `ADCS1` (`0x30`) y los dos bajos en `ADCS2[1:0]` (`0x31`),
con **`ADCS2` bit 7 = `ADCRDY`** — el bit sobre el que el retailOS gira sin
timeout. Los valores que devuelve cada entrada y su justificación están en
[04-roadmap.md](04-roadmap.md).

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

### El COP, modelado

Hecho. `hw/arm/ipod_video/ipod_video.c` crea ahora **dos** ARM:

- `PROCESSOR_ID` (`0x60000000`) responde según **quién pregunta**, mirando
  `current_cpu->cpu_index`: `0x55` al núcleo 0, `0xaa` al 1. Antes era una
  constante.
- `CPU_CTL` / `COP_CTL` (`0x60007000` / `+0x04`) duermen y despiertan de
  verdad. Escribir el bit 31 hace `cs->halted = 1` más
  `cpu_interrupt(CPU_INTERRUPT_HALT)`; escribir sin él lo revierte y da un
  `qemu_cpu_kick`. Leer devuelve el bit 31 desde el estado real del núcleo.
  Eso soporta las tres operaciones que usa el firmware: dormirse
  (`str 0x80000000`), despertar al otro (lectura, `bic` del bit 31, escritura)
  y preguntar si duerme (`lsr #31`).
- El controlador de interrupciones tiene ahora **dos salidas**, y cada banco
  aplica su propia máscara: `cpu_enabled` al núcleo 0 y `cop_enabled` al 1. Los
  registros `COP_INT_EN` ya existían en el mapa; ahora hacen algo.
- Ambos núcleos salen de reset en el mismo vector, como en el hierro.

**Funciona, y se puede comprobar.** Con `info registers` sobre el núcleo 1
durante el arranque del retailOS: `R02 = 0xaa` (leyó bien su identidad),
`R14 = 0x258` (volvió por la rama del COP), pila en IRAM, y PC parado en
`0xa057c`, justo después de:

```
a0570: ldr r1, [pc, #16]      ; 0x60007000
a0574: mov r0, #0x80000000
a0578: str r0, [r1, #4]       ; COP_CTL = SLEEP
```

O sea que el COP arranca, se reconoce, toma su camino y **se duerme a sí
mismo** como primer acto, exactamente igual que hace Rockbox en `crt0-pp.S`.
Que esté detenido ahí es el comportamiento correcto, no un fallo.

**Rockbox no ha regresado.** `navtest.sh` da la misma secuencia que la
referencia guardada en `assets/nav-baseline`: menú, la barra baja dos ítems,
SELECT entra. Las diferencias de píxeles bailan un poco (14180 → 14157) porque
el ticker del título va en otra fase.

### El chip tiene que decir quién es

Segundo arreglo, necesario y encontrado por el camino. `PP_VER1` y `PP_VER2`
(`0x70000000` / `+0x04`) deletrean el modelo en ocho caracteres ASCII. El orden
lo da `debug-pp.c` de Rockbox: los cuatro bytes de `PP_VER2` de mayor a menor
peso, luego los de `PP_VER1`.

Apple lee **un solo carácter** de todo eso, el sexto:

```
598: ldr r0, [0x70000000]     ; PP_VER1
5a4: lsr r0, r0, #24          ; tras lsl #8 -> bits 23:16
5a8: cmp r0, #0x32            ; '2' -> PP5022
```

y con la respuesta elige una dirección base distinta:

| carácter 5 | base |
|---|---|
| `'6'` | `0x40020000` |
| `'2'` (nuestro chip) | `0x40020000` |
| cualquier otra cosa | `0x40018000` |

Devolver cero no era un desconocido inofensivo: era una tercera respuesta que
el firmware no contempla, y se caía a la base de otro chip. Ahora los registros
valen `0x50503530` / `0x32324300`, que es **PP5022C**, lo que reporta el
aparato de la mesa.

### Sigue sin arrancar, y por qué

Con las dos cosas puestas, el retailOS **sigue muriendo en el mismo assert**.
La estructura de `0x40006000` no cambia: `+0x20` continúa a cero.

Lo que sí cambió es a qué mira. Con la identificación correcta,
`0x1084` devuelve `0x40020000 + 24 - 0x100 = 0x4001FF18`, y esa dirección está
**entera a ceros**. `[r4+4]` sigue siendo 0, así que el nibble de memoria sigue
saliendo 0 y el assert sigue saltando.

O sea que el bloque de parámetros que el firmware espera en lo alto de la IRAM
no lo escribe nadie. Repasando lo que hay: el propio `osos` no lo toca antes de
leerlo (solo ha ejecutado ~284 bloques), y el COP está dormido en ese momento
por diseño.

**Hipótesis que planteé y que hay que tirar**: dije que ese bloque lo dejaba
la ROM de arranque de Apple en la IRAM. **Es falsa**, y la comprobación que la
tumba es barata.

`0x1084` no calcula la dirección de un bloque de parámetros: es un
**"dame la dirección de la variable global N"**. Devuelve
`IRAM_END + N - 0x100`, o sea que el sistema guarda sus globales en los
**últimos 256 bytes de la IRAM**, y la base depende del tamaño de IRAM del
chip (96 KB en el PP5020, 128 KB en el PP5022 — de ahí `0x40018000` frente a
`0x40020000`).

Tiene 13 llamantes, y **los hay que escriben**:

```
1034: mov r0,#16 ; bl 0x1084 ; str r4,[r0]     ; global_16 = r4
b01e8: mov r0,#48 ; bl 0x1084 ; str r4,[r0]    ; global_48 = r4
bbdc8: mov r0,#44 ; bl 0x1084 ; str r1,[r0]    ; global_44 = 0
28575c:                         str r5,[r0]    ; global_44 = r5
```

y lectores simétricos (`0xfa4`, `0xfbc`, `0xf2360`, `0x285748`). Es el área de
variables globales del propio sistema, no algo heredado de la ROM. Sale a cero
sencillamente porque la IRAM arranca a cero y **nadie la ha inicializado
todavía**.

### Qué queda realmente sin explicar

El assert lee `[&global_24 + 4]`, o sea la **global del offset 28**, y la usa
como puntero: `[puntero + 0xe0]` da el nibble de configuración de memoria.

De los 13 llamantes de `0x1084`, **ninguno usa el offset 28**. Así que quien
escribe esa global o tiene la base cacheada y no pasa por el helper, o el valor
llega por otra vía. Eso es lo que falta por localizar.

### Y el COP tampoco es la explicación

La segunda comprobación: **el CPU no despierta al COP antes de necesitar ese
valor**. Su camino es `0x25c` → `bl 0x15f8` → `bl 0x1348` → `bl 0x1244` →
`bl 0x15fc` → `bl 0x27eaec` → `bl 0x13bc`, y ahí dentro:

- `0x15f8` es literalmente `bx lr`, un stub vacío.
- `0x15fc` enmascara interrupciones sobre `0x60004000` (escribe `~0` en los
  registros de deshabilitación en `+0x1c`, `+0x28`, `+0x38`, `+0x11c`, `+0x128`,
  `+0x138`).

No hay ninguna escritura a `COP_CTL` en esa ruta. Así que la lectura de que
"la global la rellena el COP y por eso falla" **tampoco se sostiene**: si
dependiese del COP, el CPU tendría que despertarlo antes, y no lo hace.

Lo que sí es cierto y se mantiene: `0x95024` está condicionada al COP
(`cmp r0,#0xaa`) y en el CPU sale sin hacer nada. Pero de ahí no se deduce que
sea ella quien pone la global 28; eso era una inferencia mía, no un hecho
comprobado.

### Estado honesto

Tres hipótesis planteadas sobre este mismo assert, tres retiradas:

1. *La flash en blanco* — falsa: el firmware no lee la flash antes de morir.
2. *La ROM deja un bloque de parámetros* — falsa: es el área de globales del
   propio sistema, con escritores dentro del `osos`.
3. *El COP rellena la global* — no sostenida: el CPU nunca lo despierta antes.

Lo verificado y sólido, que no cambia: el sistema cree que hay cero bytes de
RAM porque la global 28 vale cero, y el assert de la línea 231 salta. Lo que
falta es encontrar quién debería escribirla.


## El retailOS de Apple: dónde se para exactamente (2026-08-08)

Con la cadena del chip corregida (ver el commit de `PP_VER1`), el firmware pasa
el assert del tamaño de RAM y llega mucho más lejos. Se para aquí, y la traza
lo deja sin ambigüedad:



El vector SWI en `+0x08` es `e59ff0dc` = `ldr pc,[pc,#0xdc]`, que carga la
dirección del manejador desde `+0xec`. Ese literal vale **`0x40000008`**:
IRAM. Y la IRAM está entera a ceros, así que la CPU hace un NOP sled por ella
hasta tropezar.

Es decir: **el firmware espera encontrar sus manejadores de excepción ya
instalados en IRAM antes de hacer su primera llamada al sistema.** Nadie los
pone ahí en nuestro modelo.

Lo que falta averiguar es quién debería ponerlos:

1. El propio `osos`, en algún punto de su ruta de reset que no estamos
   recorriendo o que falla antes en silencio.
2. La ROM de arranque del PP, que nos saltamos entera. Si es esto, **el volcado
   de la flash pasa a ser necesario** — y esta vez como conclusión derivada de
   la traza, no como corazonada.

La forma de distinguirlo es buscar en el binario referencias a `0x40000000`.
**Las hay**: 139 literales alineados a palabra, el primero en `0x7f7b4`. Y
`0x40000008` aparece dos veces alineado — en `0xec` (el vector SWI) y en
`0x53f508`.

Eso inclina la balanza hacia la **hipótesis 1**: el `osos` sí tiene código que
toca la base de IRAM, así que probablemente es él quien debería instalar los
manejadores y no llega a hacerlo. Queda por comprobar si alguna de esas
referencias es realmente una copia de la tabla de vectores, y en qué punto del
arranque debería ejecutarse respecto a la primera llamada al sistema en
`0xe9b98`. La segunda aparición de `0x40000008` en `0x53f508` es el sitio por
donde empezar a mirar.

### Método

Los cuatro obstáculos de esta fase se resolvieron igual: **desensamblar el
binario en la dirección que señala la traza**, en lugar de razonar sobre qué
periférico podría faltar. Las tres hipótesis que se plantearon sobre el
hardware —flash en blanco, bloque de parámetros de la ROM, el COP— fallaron
las tres. Los literales `"PP5020A "` estaban a la vista, doce bytes después
de la comparación que los usa.

## Resuelto: quién escribe la global 28, y las tres hipótesis anteriores (2026-08-09)

Todo lo de arriba sobre el assert de la línea 231 queda cerrado aquí. La
pregunta que quedaba abierta —*"de los 13 llamantes de `0x1084`, ninguno usa el
offset 28; quién escribe esa global"*— tiene una respuesta y no es ninguna de
las tres candidatas que se habían planteado.

**La escribe la ROM de arranque de Apple, y la hipótesis 2 queda por tanto
reinstaurada** — pero no como se había formulado. Las dos cosas eran ciertas a
la vez y por eso se descartó de más:

- `0x1084` **sí** es un "dame la dirección de la global N", y hay llamantes que
  escriben. Eso era correcto.
- Y **además** las entradas 24 y 28 de esa misma área son el par
  `{tag, puntero}` que iPodLinux llama SysInfo, en `0x4001ff18` y `0x4001ff1c`.
  Que el `osos` use ese área para sus propias globales no impide que la ROM
  haya dejado dos palabras en ella antes.

La comprobación que lo cierra: la constante `0x53797349` (`"IsyS"`) la carga
**una sola instrucción de toda la imagen**, `0x13fc`, y es una *comparación*.
Nunca se escribe. Un valor que el firmware solo compara y jamás produce tiene
que venir de fuera del firmware.

Coincide exactamente con `ipodloader2/ipodhw.c` de iPodLinux, que lee esas dos
direcciones y comenta que los últimos 256 bytes de la IRAM los usa la Flash
ROM.

### Las tres hipótesis, en su sitio definitivo

| Hipótesis | Veredicto final |
|---|---|
| *La flash en blanco es el problema* | **Sigue falsa.** El firmware no lee la flash antes de morir, y tampoco lee `0x2000405c`: `findlit.py` no encuentra ni una sola instrucción que cargue ese literal. |
| *La ROM deja un bloque de parámetros* | **Reinstaurada, con matiz.** No es "el área de `0x1084` es de la ROM": es que dos palabras concretas de esa área las deja la ROM. |
| *El COP rellena la global* | **Sigue sin sostenerse**, y ahora además es innecesaria. |
| *El `osos` tiene 139 referencias a IRAM* | **Sigue falsa** (eran constantes de coma flotante), pero la conclusión que se sacaba de ella —que el `osos` instala sus propios manejadores— resultó **cierta**: lo hace en `0x2a0` → `0x83848`. |
| *Hace falta un volcado de la flash* | **No para arrancar.** Sí sería la fuente ideal para conocer el resto del bloque SysInfo, que hoy va a ceros. |

### Y una que no se había planteado: el bit 26

El muro que quedaba entre "pasa el assert" y "arranca el núcleo" no era ningún
periférico: era el mapa de memoria. El controlador de SDRAM no decodifica el
bit 26 de la dirección, así que toda dirección de DRAM tiene una gemela
`0x04000000` más arriba, y el firmware de Apple usa las dos indistintamente.
La derivación completa está en `04-roadmap.md` y en el comentario de
`DRAM_ALIAS_BIT`.

De paso, dos correcciones a la unidad de remap descritas más arriba:

- La ventana que pone la DRAM en la dirección 0 para el firmware de Apple es
  **MMAP1**, no MMAP0. Su MMAP0 (lógico `0x00003bf0` / físico `0x00003a88`)
  apunta a la física 0. Funcionaba por accidente porque el modelo activaba el
  remap ante cualquier escritura a MMAP0.
- La máscara: el bit *k* del registro lógico cubre el bit *k+16* de la
  dirección. Sale de comparar las dos que usa `crt0-pp.S` — `0x3e00` para una
  ventana de 32 MB y `0x3c00` para 64 MB: quitar el bit 9 duplica la ventana,
  luego el bit 9 es el bit 25 de la dirección. Sigue sin aplicarse, pero ya
  está documentada.

### Método, revisado

La lección de la fase anterior —desensamblar la dirección que señala la traza
en vez de razonar sobre qué periférico podría faltar— se mantiene, con una
corrección importante: **antes de eso, dejar hablar al firmware**. El impresor
de pánico llevaba tres commits siendo interpretado como "la primera llamada al
sistema". Diez segundos de semihosting habrían dicho `Type: _sys_exit` y cuatro
bancos de memoria vacíos.
