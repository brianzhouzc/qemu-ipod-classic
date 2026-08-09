# Roadmap

## Dos objetivos, dos máquinas

El proyecto arrancó apuntando al **iPod classic 6G** (S5L8702) y se topó con que
su firmware está cifrado con una clave que vive en el silicio. Al aparecer un
**iPod Video 5G** físico, cuyo firmware está en claro, el objetivo de arrancar
el sistema original de Apple se movió allí.

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Classic` | S5L8702 | ejecuta código, timers e interrupciones verificados |
| `iPod-Video` | PP5022C | **Rockbox se navega entero; el retailOS de Apple arranca su núcleo y se queda esperando al PMU** |

El trabajo del Classic sigue intacto y es reaprovechable si algún día
conseguimos un `osos` descifrado.

## iPod Video (PP502x) — estado

Con `-M iPod-Video,entry=0x735a00 -kernel osos_full.bin`, el bootloader de
Rockbox extraído del aparato real recorre esta secuencia:

1. Lee `PROCESSOR_ID` (`0x60000000`) y se reconoce como CPU
2. Espera a que el COP declare que duerme (`COP_CTL`, bit 31)
3. Se copia a sí mismo a IRAM (`0x40000000`)
4. Configura GPIO — seis escrituras de dirección de pines
5. Consulta la revisión de hardware en `0x405c`
6. Hace **transacciones I²C** contra el PMU y el códec (`0x7000c100`/`0x104`/`0x120`)
7. Llega al **controlador IDE** (`+0x1fc`), buscando `rockbox.ipod` en la FAT32

Modelado hasta ahora: DRAM, IRAM, ventana de flash NOR con la revisión de
hardware, `PROCESSOR_ID`, `CPU_CTL`/`COP_CTL`, `USEC_TIMER`, UART 16550,
controlador de interrupciones, IDE, click wheel, GPIO, caché, remap, BCM/LCD,
reloj/PLL y control de dispositivo.

### El bootloader ya pinta en la pantalla emulada

Estado actual: el bootloader de Rockbox 4.0 arranca entero y escribe en el LCD:

```
No partition
found (0).
Insert USB cable
and fix it.
4.0
```

Para llegar hasta ahí hubo que quitar cuatro bloqueos, todos del mismo tipo —
un bit de estado que el hardware real pone y nosotros devolvíamos a cero, con
el firmware esperándolo en un bucle sin salida:

| Síntoma | Causa | Arreglo |
|---|---|---|
| bucle leyendo `0x6000603c` | `PLL_STATUS` bit 31 = PLL enganchado. `system-pp502x.c`: `while (!(PLL_STATUS & 0x80000000));` | bloque de reloj en `0x60006000`, con el bit 31 siempre puesto |
| `Invalid write at 0xF0004000` | `cache_invalidate_special()` escribe a mano todo el array de estado de la caché, que no estaba mapeado | RAM en `0xf0000000`–`0xf000efff` |
| 20 M lecturas de `0x70000028` | bit 7 = el bloque USB salió de reset. `usb-fw-pp502x.c`: `while ((inl(0x70000028) & 0x80) == 0);`. Ese registro **no aparece en `pp5020.h`** | bloque `DEV_CTL` en `0x70000000`, con el bit 7 siempre puesto |
| todo corría pero la pantalla seguía negra | `GPO32_VAL` bit 14 alimenta al Broadcom. Si está a 0, `lcd-video.c` cree que debe inicializar el BCM subiendo su firmware `vmcs` desde la NOR (que no tenemos), deja `display_on = false`, y **`lcd_update_rect()` descarta todos los dibujados sin decir nada** | `GPO32_VAL` arranca con `0x4000`, reproduciendo lo que deja el cargador de Apple |

La lección: en este SoC casi todo fallo se manifiesta igual — el emulador
parece vivo, no hay ninguna excepción, y el firmware está esperando un bit.
`-d unimp` con `sort | uniq -c | sort -rn` encuentra el bucle en segundos.

Ojo con `-d unimp`: cuando el guest entra en un bucle cerrado sobre un registro
sin modelar, el log llega a decenas de millones de líneas y frena la emulación
tanto que parece colgada. Conviene medir siempre sin `-d` antes de concluir que
algo no avanza.

### Rockbox arranca de verdad

Capturando la pantalla en varios instantes se ve que el `No partition found` no
es el final, es el **segundo** fotograma:

| t | Pantalla |
|---|---|
| ~8 s | el **logo de arranque de Rockbox** a color: "ROCKBOX — open source jukebox firmware", "Ver. 4.0" (538 colores, 22 000 píxeles) |
| ~20 s en adelante | `No partition found (0). Insert USB cable and fix it.` |

O sea que la secuencia completa funciona: el bootloader monta el FAT32, carga
`rockbox.ipod`, salta a él, y **la aplicación Rockbox arranca y dibuja su
logo**. El error es del `disk_mount_all()` de la *app*, no del bootloader.

### El bloqueo real: la app usa DMA, el bootloader no

`-trace "ide_*"` separa las dos fases sin ambigüedad:

```
# bootloader
cmd 0xec  IDENTIFY
cmd 0xef  SET FEATURES  (x2)
cmd 0xc6  SET MULTIPLE MODE
cmd 0xc4  READ MULTIPLE   <- x40, cargando rockbox.ipod
# app
cmd 0xec  IDENTIFY
cmd 0xef  SET FEATURES  (x3)
cmd 0xc6  SET MULTIPLE MODE
cmd 0xec  IDENTIFY
cmd 0xc8  READ DMA        <- y aqui se rompe
ide_reset                 <- inmediatamente despues
cmd 0xec / 0xef / 0xc6 / 0xec / 0xe7 FLUSH CACHE
```

Y el sector 0 solo se lee dos veces, las dos al principio: el
`disk_mount_all()` de la app ni siquiera llega a `disk_init()`.

`ipodvideo` define `HAVE_ATA_DMA`, así que la app pide **READ DMA (0xc8)**
mientras que el bootloader se queda en PIO. Nosotros usamos el `mmio-ide`
genérico de QEMU, que **no tiene motor de DMA**: la transferencia nunca ocurre,
`ata_wait_intrq()` agota sus 10 segundos, el driver resetea el bus y se rinde.

Lo que descartamos por el camino (todo correcto, no tocar): la tabla de
particiones de la imagen, el FAT32 (512 B/sector, 2 FATs, `FATsz32 = 4033`,
`dataclusters ≈ 516190`, muy por encima del mínimo de 65525 de
`fat_mount_internal`), y `BPB_HiddSec = 0`, que Rockbox no mira.

### Resuelto: motor de DMA del IDE del PP502x

En el PP502x el DMA no es bus-master PCI, son registros propios del bloque IDE.
De `ata-target.h` y `ata-pp5020.c`:

| Registro | Dirección | Función |
|---|---|---|
| `IDE0_CFG` | `0xC3000028` | bit 3 `INTRQ` (pendiente, se limpia escribiendo 1), bit 15 activo durante el DMA |
| `IDE_DMA_CONTROL` | `0xC3000400` | bit 0 = **arranca**, bit 1 = arma, bit 3 = dirección (1 = lectura a memoria) |
| `IDE_DMA_LENGTH` | `0xC3000408` | bytes **menos 4** |
| `IDE_DMA_ADDR` | `0xC300040C` | dirección física destino |

La dirección y la longitud se programan **antes** del comando ATA y el bit de
arranque **después**, así que `start_dma` solo puede guardar el callback que le
pasa el núcleo IDE; es la escritura del bit 0 la que dispara la transferencia.
Es el mismo reparto que usa el IDE bus-master de PCI, por el mismo motivo.

Está en `hw/arm/ipod_video/ipod_video_ide.c`, un dispositivo `pp502x-ide` que
se queda con toda la ventana `0xc3000000`–`0xc3000fff`: el task file, los
registros de configuración y temporización, y el DMA. Sustituye al `mmio-ide`
genérico más el stub que había al lado.

Una trampa que costó un ciclo: `ide_bus_init_output_irq()` **termina** con
`bus->dma = &ide_dma_nop`. Si reclamas el bus antes de llamarla, se pierde sin
ningún aviso y todos los comandos DMA se aceptan y se ignoran. Hay que
asignarlo después.

`INTRQ` es un **pestillo**, no un espejo de la línea de interrupción: durante el
DMA el firmware nunca lee el estado ATA (hacerlo "causes failures and hangs"),
así que ese bit es su única señal de fin. Se pone en el flanco de subida y se
borra escribiendo un uno.

### El click wheel: dos cosas más

Con el disco montado, Rockbox arrancaba entero pero no respondía a las teclas.
En el PP la rueda es **por interrupción**, no por sondeo: `ipod_4g_button_read()`
solo se llama desde `ipod_4g_button_int()`, y ni siquiera mira los datos si no
hay un bit de estado puesto:

```c
if ((inl(0x7000c104) & 0x04000000) != 0) {
    unsigned status = CLICKWHEEL_DATA;   /* 0x7000c140 */
    ...
}
/* y el handler reconoce con: */
outl(inl(0x7000c104) | 0x0c000000, 0x7000c104);
```

Así que una pulsación tiene que hacer dos cosas: poner el bit 26 de `0x7000c104`
y levantar la interrupción de la rueda, que en este SoC es **la línea de I²C del
banco alto** (`I2C_IRQ = 32+8`). Sin las dos, el firmware no lee la rueda jamás.

El giro va en el mismo paquete: bit 30 = rueda tocada, posición en los bits
16..22, 96 clics por vuelta, el sentido horario incrementa. Rockbox exige
`WHEEL_SENSITIVITY` (4) clics acumulados para mover la selección un ítem, así
que cada evento de tecla avanza dos y una pulsación completa (presionar +
soltar) mueve exactamente uno.

## Estado: se navega el menú

`tools/ipod_video/navtest.sh` lo comprueba solo: arranca, sale de *Now Playing*
con MENU, gira la rueda y selecciona, capturando la pantalla en cada paso.

| Fotograma | Resultado |
|---|---|
| arranque | pantalla *Now Playing* con tema, carátula, barra de progreso y batería |
| MENU | menú principal: Files / Database / Now Playing / Settings / Recording / Playlists / Plugins / System |
| rueda horaria ×2 | la barra de selección baja de *Now Playing* a *Settings* y luego a *Recording* |
| SELECT | entra en *Recording* (Time, Size, Volume −28 dB, Gain) |
| rueda antihoraria ×3 | la selección vuelve a subir |

Detalle real, no fallo del modelo: **la primera pulsación después de cambiar de
sentido no mueve nada**. Rockbox reinicia su acumulador al cambiar de
dirección, así que se come esos cuatro clics. Se ve en las cifras del test: el
fotograma siguiente al cambio de sentido difiere ~2 300 píxeles (solo el ticker
del título, que se desplaza solo) y el siguiente ya ~14 000 (la barra movida).

Ojo con eso al medir: la pantalla *Now Playing* se anima sola, así que una
diferencia pequeña de píxeles **no prueba nada**. Solo vale la barra movida.

### Lo que sigue sin modelar

- **El COP.** `core_thread_init()` despierta al segundo núcleo y se duerme
  esperándolo; como nuestro `sleep_core` no duerme, el CPU sigue y arranca
  igual, pero todo lo que Rockbox programe en el COP no se ejecuta.
- **Audio.** El I²S y el códec no están; la reproducción no suena.
- **Escritura por DMA.** Solo se ha ejercitado la lectura. El camino de
  escritura está implementado pero no probado.
- **La rueda no genera "untouch"**: una vez tocada se queda tocada.

## El retailOS de Apple: de pantalla negra a núcleo arrancado

Con `-M iPod-Video,entry=0` el firmware original de Apple pasó de morir en la
instrucción 289 a ejecutar 5175 bloques, arrancar su núcleo, levantar sus dos
núcleos y entrar en el bucle de reposo de su planificador. **La pantalla sigue
negra**, y ahora sabemos exactamente por qué.

### Lo primero: no estaba callado, estaba entrando en pánico

La dirección donde acababan todas las trazas, `0xe9b98`, no era "la primera
llamada al sistema del firmware". Es el impresor de pánico de la librería C de
ARM. La instrucción de encima lo delata:

```
e9b94:  mov r0, #4          @ SYS_WRITE0
e9b98:  svc 0x123456
```

y la tabla de cadenas justo arriba contiene `"Type: "`, `"undefined
instruction"`, `"data abort"`, `"_sys_exit"`, `"CPU "`, `"Task"`. Con
semihosting activado, el firmware dice él mismo de qué se muere. La máquina lo
activa por defecto; `-M iPod-Video,semihosting=off` lo desactiva.

**Se retira** la hipótesis de que el retailOS moría en una llamada al sistema, y
con ella la de que la IRAM vacía era la causa: los manejadores los instala el
propio `osos` en `0x2a0`, unos 150 bloques *después* del punto de muerte.

### Los tres muros, en orden

| # | Muro | Diagnóstico | Arreglo |
|---|---|---|---|
| 1 | `svc 0x123456` sin manejador | es el impresor de pánico, no una syscall | responder al semihosting |
| 2 | data abort en `0x84390` leyendo `0x0471619c` | **el bit 26 de la dirección no lo decodifica el controlador de SDRAM**: toda dirección de DRAM tiene gemela 0x04000000 más arriba | alias de DRAM en `0x04000000` y `0x14000000` |
| 3 | data abort en `0x2668e4` sobre un objeto nulo | falta el **bloque SysInfo** que deja la ROM de arranque en lo alto de la IRAM | escribir el tag, el puntero y tres campos |

#### El bit 26

Lo dice la propia tabla de scatter-load del firmware, en
`0x84040..0x840a8`. Orígenes y destinos van en una tirada contigua del fichero
y en mitad de esa tirada la dirección salta exactamente `0x04000000`:

```
src 0x006c9a64 + len 0x0004c734 = 0x00716198 ; siguiente src = 0x0471619c
dst 0x1493690c + len 0x0004c734 = 0x14983040 ; siguiente dst = 0x14983040
```

Una discontinuidad de un bit dentro de una serie de offsets consecutivos no es
un cambio de dirección: es la misma dirección con una bandera. Lo confirma el
contenido: el offset `0x7161a4` del fichero es `e1b0f00e`, `movs pc, lr`; la
copia de `0x83848` lo deja en `0x40000008`, que es justo a donde apunta el
literal del vector SWI en `0xec`. Encaja al byte.

Lo **inferido** es qué significa el bit para el hardware. La unidad de remap
apunta en la misma dirección: MMAP1 se programa con lógico `0x00003a00` /
físico `0x10000f84`, y comparando esa máscara con las dos que usa
`crt0-pp.S` de Rockbox (`0x3e00` para una ventana de 32 MB, `0x3c00` para
64 MB) sale que el bit *k* de la máscara cubre el bit *k+16* de la dirección
— o sea que los bits 26 y 27 llegan intactos al bus físico. Alguien más abajo
tiene que estar ignorando el bit 26, y el único candidato es el controlador de
memoria.

#### El bloque SysInfo

`ipodloader2/ipodhw.c` de iPodLinux nombra las direcciones y comenta que *"los
últimos 256 bytes parecen usarlos la Flash ROM para cosas especiales"*:

```c
#define SYSINFO_TAG_PP5022 0x4001ff18
#define SYSINFO_PTR_PP5022 0x4001ff1c
```

El firmware de Apple coincide al byte: `0x1084` reparte globales en
`IRAM_END - 0x100 + N`, y `0x13bc` pide `N = 24`, compara la palabra de ahí
contra `0x53797349` (`"IsyS"`) y toma un puntero de `+4`. Nada en la imagen
escribe ninguna de las dos palabras — la constante mágica la carga **una sola
instrucción** en siete megabytes y solo para compararla — así que las dejó la
ROM que nos saltamos, igual que el bit 14 de `GPO32_VAL`.

Sin el bloque se rompen dos cosas, y la segunda solo se vio al arreglar la
primera:

- **El tamaño de RAM.** `0x13e8` lee `SysInfo+0xe0` como cuenta de bytes y
  `0xe6c` la redondea contra la tabla de `0x16d8` al tamaño del banco 2. Con el
  puntero nulo esa lectura va a `[0x000000e0]`, que tras el remap es una
  palabra de la propia tabla de vectores, `0x00000100`; 256 bytes redondean a
  cero, los cuatro bancos salen vacíos y salta el assert de `0xda0`. Lo
  llamativo: el firmware ya traía la respuesta buena — `0x1f4` hace
  `mov fp, #0x700`, cuyo nibble 2 indexa la misma tabla a `0x02000000`, los
  32 MB de un 5G — y la tira.
- **El identificador de hardware.** Esto es lo que el análisis previo tenía al
  revés. Dejar el tag a cero parece inocuo, porque `0x13e8` desreferencia el
  puntero *antes* de que `0x1400` compruebe la magia. Pero `0x140c` solo
  publica el puntero en `0x4000608c` si la magia cuadró, y esa palabra sí
  importa:

  ```
  0x2b8   bl 0x1658    memcpy(0x14983060, 0x40006000, 0x90)
  0x2bc   bl 0x281224  lee [0x149830ec]; si es nulo vuelve al instante, y si no
                       copia min(len,248) bytes a 0x108710dc y lo instala como
                       el singleton de información de hardware
  0x2d0   bl 0x2668c4 -> 0x111c58 -> 0x2653a4 -> 0x265164, que lee +0x84
  ```

  Con el tag a cero `0x281224` no hace nada, `0x265164` lee `+0x84` de un
  objeto en bss que nadie rellenó y saca cero, la tabla de saltos de
  `0x2653a4` responde "no conozco esa máquina", `0x111c58` devuelve un driver
  nulo y `0x2668e4` lo desreferencia.

Se escriben **cuatro campos y ni uno más**: la magia en `0x4001ff18`, la
longitud en `+0x04`, el identificador de hardware en `+0x84` (que es
`IPOD_VIDEO_HW_REVISION`, el valor que esta máquina ya declaraba, sacado del
aparato real por la pantalla de HW info de Rockbox — su mitad alta, `0x000b`,
es la entrada 11 de la tabla de Apple, viva) y el tamaño de SDRAM en `+0xe0`.
El resto queda a cero: el número de serie, el GUID de FireWire, el nombre de
placa y las revisiones de imagen no los sabemos, y el `sysinfo_t` de iPodLinux
**no sirve como fuente** — pone `sdram_size` en `+0x80` mientras este firmware
lee `+0xe0`, que cae dentro de un `pad` que iPodLinux nunca decodificó.

### Dónde se queda ahora: el PMU

El sistema está **vivo**, no colgado:

- ambos núcleos ejecutan (el CPU en el driver de I²C, el COP girando sobre un
  cerrojo en `0x40000fac`),
- se toman ~700 interrupciones por segundo,
- se recorre toda la secuencia de reset (`0x2668c4`, `0x276fe8`, `0xbbdc4`,
  `0x330`) y se llega al `main` del OS en `0xaf468`,
- el planificador entra en su bucle de reposo en `0x285730`.

Y el CPU se pasa el resto del tiempo en el bloque de I²C de `0x7000c000`,
repitiendo eternamente esta transacción:

```
CTRL=0x80  ADDR=0x10  DATA0=0x30    escribir el puntero de registro 0x30
CTRL=0x80  ADDR=0x11                 direccionar para lectura
CTRL=0xa2  ADDR=0x11                 leer 2 bytes  (0xa2 = SEND | lectura | len 2)
```

Dirección `0x10` >> 1 = **`0x08`, el PCF50605**, el chip de gestión de energía
(`i2c_readbytes(0x08, ...)` en `pcf50605.c` de Rockbox). Registro **`0x30` =
`ADCS1`**, y leyendo dos bytes se lleva también `ADCS2`: son los **registros de
resultado del conversor A/D**, o sea la medida de la batería.

El mapa del bloque, de `i2c-pp.c`:

| Registro | Offset | Bits |
|---|---|---|
| `I2C_CTRL` | `+0x00` | bit 7 `SEND`, bit 5 = lectura, bits 2:1 = longitud−1 |
| `I2C_ADDR` | `+0x04` | dirección de 7 bits desplazada, bit 0 = lectura |
| `I2C_DATA0..3` | `+0x0c`,`+0x10`,`+0x14`,`+0x18` | |
| `I2C_STATUS` | `+0x1c` | bit 6 `BUSY`; el driver de Apple lee además los bits 2:0 como código de resultado (`0x282f40`) |

Hoy el bloque es un fichero de registros: `STATUS` siempre lee cero, así que
toda transacción "termina" al instante y con éxito, y el firmware se lleva de
vuelta lo último que él mismo escribió. Rockbox sobrevive a eso porque su
driver lee lo que haya y sigue; el de Apple se queda dando vueltas.

**El siguiente paso, acotado:** modelar el controlador I²C del PP502x y colgar
de él un PCF50605 en la dirección `0x08` que al menos responda `ADCS1`/`ADCS2`.
El mapa de registros del PMU está en
`assets/rockbox/firmware/export/pcf5060x.h`. Lo que **no** tenemos es la escala
del ADC ni los valores que el retailOS considera aceptables, así que ese
número habrá que sacarlo de cómo lo interpreta el firmware, no inventarlo.

Queda por comprobar, y no está descartado, que la lectura de la batería sea el
único bloqueo: puede haber más consumidores del PMU detrás.

## Estrategia (iPod classic 6G)

El `osos` de Apple está cifrado con la clave GID del SoC
(ver [01-firmware-ipsw.md](01-firmware-ipsw.md)), así que no es un objetivo
inicial razonable. **El primer objetivo es arrancar Rockbox**, que es open
source, se compila sin firmar, tiene drivers a nivel de registro para todo el
S5L8702 y se carga en `0x08000000` — exactamente igual que el `osos`.

Ventaja de rebote: los drivers de Rockbox son la especificación ejecutable
contra la que validar cada periférico que modelemos.

## Hitos

### H0 — Infraestructura ✅
- [x] Clonar `devos50/qemu-ios`, crear rama `ipod_classic_6g`
- [x] Compilar el baseline (`arm-softmmu`) en WSL
- [x] Herramienta de parseo del firmware (`tools/ipod_classic/fwtool.py`)
- [x] Extraer `osos` / `aupd` del `.ipsw`
- [x] Mapa de memoria y de IRQs del S5L8702 documentado

### H1 — Máquina mínima que ejecuta instrucciones ✅
- [x] `include/hw/arm/ipod_classic.h` con la tabla de bases del S5L8702
- [x] `hw/arm/ipod_classic.c`: CPU ARM926EJ-S, SDRAM `0x08000000` (64 MB),
      IRAM `0x22000000` (256 KB)
- [x] Entradas en `hw/arm/meson.build`, `hw/arm/Kconfig` y
      `configs/devices/arm-softmmu/default.mak` (`CONFIG_IPOD_CLASSIC_6G`)
- [x] Ruta de arranque tipo `-kernel`: cargar un binario en `0x08000000` y
      poner ahí el PC (saltarse la bootrom, que no tenemos)
- [x] Los 32 periféricos aún no modelados registrados como
      `create_unimplemented_device()`, para que `-d unimp` diga qué toca el guest
- [x] Comprobar con un `.bin` ARM trivial (`tools/ipod_classic/mktestbin.py`)

**Resultado:** `-M iPod-Classic` aparece en `-M help` y el binario de prueba
escribe `S5L8702 alive` por la consola serie. Eso valida de golpe: PC inicial en
`0x08000000`, SDRAM mapeada, y UART0 en la dirección correcta.

```
$ ./qemu-system-arm -M iPod-Classic -kernel hello.bin \
      -display none -monitor none -serial stdio -d unimp
S5L8702 alive
```

### H2 — Consola serie e interrupciones ✅
- [x] VIC0/VIC1 (PL192) en `0x38E00000` / `0x38E01000`
- [x] Reloj/PLL en `0x3C500000` (modelo reutilizado del iPod Touch)
- [x] UART0 en `0x3CC00000` (exynos4210_uart, IRQ 24)
- [x] Timers en `0x3C700000` — bloque completo escrito de cero
      (`hw/arm/ipod_classic/ipod_classic_timer.c`)
- [x] Alias de IRAM0 en la dirección `0x0`, que es de donde el ARM926 coge las
      excepciones
- [x] Ruta de interrupción verificada de extremo a extremo

**Resultado:** el test `tools/ipod_classic/tests/timer_irq.c` configura el timer
B a 10 kHz exactamente como Rockbox, instala una tabla de vectores en `0x0`,
desenmascara la IRQ 8 en VIC0 y cuenta interrupciones:

```
S5L8702 timer test
counter runs: yes 0x00000001 0x00000002
waiting for interrupts...
got 0x00000005 timer interrupts
TIMER IRQ OK
```

El log de `-d unimp,guest_errors` sale vacío: ningún acceso a periférico no
modelado, ninguna dirección fuera de sitio.

#### El bloque de timers

Reconstruido a partir de `timer-s5l8702.c`, `kernel-s5l8702.c` y el comentario
sobre `TSTAT` en `s5l87xx.h`:

- 8 canales. **A–D de 16 bits comparten la IRQ 8**; **E–H de 32 bits comparten
  la IRQ 7**.
- El estado de interrupción de A–D vive **dentro de `CON[18:16]`**, con los
  habilitadores en `CON[14:12]` — por eso Rockbox escribe `TBCON = TBCON` para
  acusar recibo, y por eso comprueba `TACON & (TACON >> 4) & 0x7000`.
- El de E–H vive en `TSTAT` (`0x118`), tres bits por canal: E en 24, F en 16,
  G en 8, H en 0. Se limpia escribiendo unos.
- Selector de reloj en `CON[10:8]`: /2, /4, /16, /64, o directo para E–H.
  `CON[6]` elige ECLK (12 MHz, el cristal) frente a PCLK.
- **PCLK no está modelado**: el bloque de reloj es un fichero de registros sin
  lógica, así que no sabemos qué PLL pidió el guest. Los dos timers que usa
  Rockbox eligen ECLK, así que de momento se trata PCLK como ECLK y se avisa
  por `-d unimp` si algún guest lo selecciona de verdad.
- Solo se emula el modo intervalo, que es el único que usa Rockbox. Los demás
  se reportan como no implementados en vez de hacer algo incorrecto en silencio.

#### IRAM0 en la dirección 0

El `crt0.S` de Rockbox lo dice sin ambigüedad:

> *we do not want to execute from 0x0 as iram will be mapped there*
> *copy vector section to start of IRAM0 (it will be mapped to 0x0)*

Así que IRAM0 aparece también en `0x0`, que es de donde el ARM926 toma las
excepciones. Está modelado como alias. **Lo que aún no sabemos** es si la
bootrom ocupa esa dirección primero y el alias solo aparece tras un remap, como
en el S5L8720. Hasta que haya un volcado de bootrom que lo aclare, si se pasa
una bootrom esta se queda con la dirección.

### H2b — Arrancar Rockbox
- [ ] Toolchain: `apt install gcc-arm-none-eabi` sirve para nuestros tests, pero
      Rockbox quiere `arm-elf-eabi-gcc` de `tools/rockboxdev.sh`
- [ ] Compilar el bootloader de Rockbox para ipod6g
- [ ] Lanzarlo con `-kernel` y seguir `-d unimp` como lista de tareas

**Criterio de éxito:** ver la salida de depuración de Rockbox por serie.

### H3 — Pantalla
- [ ] Controlador LCD en `0x38300000` + ventanas en `0x39200000` (IRQ 14)
- [ ] Scanout 320×240 RGB565 a `ui/console`

**Criterio de éxito:** el logo o el menú de Rockbox aparece en la ventana SDL.

### H4 — Entrada
- [ ] Click wheel en `0x3C200000` (IRQ 23), mapeado a teclado/rueda del ratón
- [ ] GPIO de botones

**Criterio de éxito:** navegar por el menú de Rockbox.

### H5 — Almacenamiento
- [ ] Controlador ATA en `0x38700000` (IRQ 29), respaldado por un archivo de
      imagen de disco
- [ ] Alternativa/complemento: CE-ATA sobre SDCI en `0x38b00000` (IRQ 44)

**Criterio de éxito:** Rockbox monta el FAT32 del disco y reproduce un archivo.

### H6 — Audio
- [ ] I²S en `0x3CA00000` + códec CS42L55 por I²C

### H7 (especulativo) — el OS de Apple
- [ ] Modelar el motor AES en `0x38c00000` con la clave GID del S5L8702
- [ ] Bootrom del S5L8702 (hace falta un volcado)
- [ ] Descifrar y arrancar `osos`

Este hito depende de material que ahora mismo no tenemos. Se replantea cuando
lleguemos.

## Estado actual

En H0 completo, empezando H1.
