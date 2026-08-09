# Roadmap

## Dos objetivos, dos máquinas

El proyecto arrancó apuntando al **iPod classic 6G** (S5L8702) y se topó con que
su firmware está cifrado con una clave que vive en el silicio. Al aparecer un
**iPod Video 5G** físico, cuyo firmware está en claro, el objetivo de arrancar
el sistema original de Apple se movió allí.

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Classic` | S5L8702 | ejecuta código, timers e interrupciones verificados |
| `iPod-Video` | PP5022C | **Rockbox se navega entero, y el retailOS de Apple también: arranca, dibuja su interfaz y responde a la rueda y a los botones** |

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
hardware, **los dos núcleos ARM** (CPU y COP, que duermen y despiertan de
verdad), `PROCESSOR_ID`, `CPU_CTL`/`COP_CTL`, `USEC_TIMER`, UART 16550,
controlador de interrupciones, IDE con DMA ATA, click wheel, GPIO, caché,
remap (con los alias del bit 26 y del bit 31), **los dos bloques de DMA del
PP502x**, **el I²C y el PCF50605**, el BCM2722 con **la interfaz de host del
VideoCore**, reloj/PLL, control de dispositivo y **semihosting de ARM**.

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

> **Corregido el 2026-08-09**: esta lista abría con *"El COP: como nuestro
> `sleep_core` no duerme, el CPU sigue y arranca igual"*. Dejó de ser cierto en
> el commit `9d823ec76`: hay **dos** núcleos ARM y los dos se duermen y
> despiertan de verdad. El buzón CPU-COP de `0x60001000` sí sigue sin modelar.

- **Audio.** El I²S y el códec no están; la reproducción no suena.
- **El buzón CPU-COP** de `0x60001000`, que cae en el suelo de `-d unimp`.
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

### Dónde se quedaba: el PMU (resuelto, 2026-08-09)

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

## El retailOS arranca del todo (2026-08-09)

Con el I²C y el PMU modelados —más dos muros que aparecieron detrás— el
retailOS deja de bloquearse: termina su arranque, entra en el bucle de reposo
del planificador y no imprime ni un pánico. **La pantalla sigue negra**, y
ahora se sabe exactamente qué falta y por qué.

Capturas a los 10, 30, 60 y 90 s: **0 píxeles encendidos en las cuatro**. Sin
adornos: el retailOS no dibuja.

### Los tres muros de esta sesión

| # | Muro | Cómo se encontró | Arreglo |
|---|---|---|---|
| 1 | bucle infinito leyendo `ADCS2` del PCF50605 | ya estaba acotado | controlador I²C `pp502x-i2c` + esclavo `pcf50605` |
| 2 | data abort leyendo `0x93EE1B70` | el propio firmware lo imprime por semihosting, en bucle | DRAM también responde con el bit 31 puesto |
| 3 | bucle sobre el bit 6 de `BCM_ALT_CONTROL` | `info registers` en el monitor: `R01=0x30070000` | los dos bits del *handshake* de arranque del Broadcom |

#### 1. El I²C y el PMU

El bloqueo era `pmu_adc_read` (`0x28341c`) girando **sin timeout** sobre el bit
7 de `ADCS2`, el "conversión lista" del PCF50605. Hasta ahora `0x7000c000` era
un fichero de registros plano, así que toda transacción "funcionaba" y devolvía
el último byte que el propio firmware había escrito.

Dos dispositivos nuevos, `hw/arm/ipod_video/ipod_video_i2c.c` y
`ipod_video_pcf50605.c`. Los detalles del bloque están en
[07-pp502x-hardware.md](07-pp502x-hardware.md). Lo que conviene retener:

- **El firmware de Apple lee `STATUS[2:0]` como código de error** y aborta la
  transacción entera si no es cero. Rockbox no lo mira. Un fichero de
  registros plano lo cumplía por accidente.
- **El bit de arranque tiene que autoborrarse.** No es una suposición: todos
  los pasos siguientes de los dos drivers son lecturas-modificación-escritura
  de `CTRL`, así que un bit que se quedara puesto dispararía una segunda
  transferencia no pedida en la siguiente, y contra un esclavo con puntero de
  registro autoincremental eso lee bytes equivocados para siempre.
- **El bloque tiene que funcionar sin inicialización.** La rutina de
  `DEV_EN`/`DEV_RS` de Apple (`0x282e98`) solo se alcanza cuando una
  transferencia expira; si `STATUS` nunca dice `BUSY`, el guest nunca habilita
  el bloque.

**Los valores del ADC, y de dónde sale cada uno** — esto es lo que la regla de
"no inventar" exigía:

| Entrada del mux | Valor | Justificación |
|---|---|---|
| 2, `ADCIN1_RES` (batería de Rockbox) | 682 | Rockbox lo convierte a milivoltios con su propio `BATTERY_SCALE_FACTOR` de 6000 por 1024 cuentas, así que son 4000 mV. Su tabla del iPod Video pone el 0 % en 3600 mV y el 100 % en 4180. Rockbox muestra ahora **77 %**, que es exactamente lo que da interpolar 3996 mV en esa tabla |
| 3, `ADCIN1_SUBTR` (batería de Apple) | 390 | **SUPUESTO**, pero dentro de un rango que define el firmware: `0x264ee8` busca la cuenta cruda en una tabla de quince umbrales (283…454) y devuelve el índice, y `0x264cb0` no enciende un raíl por debajo del literal `0x12a` = 298 de `0x264d20`. 390 cae entre el décimo y el undécimo umbral, lejos de los dos extremos. **Cuántos voltios son, no se sabe, y nada depende de saberlo** |
| 4 y 7, identificadores de accesorio | fondo de escala | `0x266034` toma los cuatro bits altos como cubeta y convierte la cubeta 15 en el código 100, que es el valor con el que la tarea `"RsistrAccsryMgr"` (`0xfec04`) se inicializa. Una línea de identificación sin nada enchufado está en circuito abierto y lee fondo de escala, que es esa misma cubeta. Las dos cosas coinciden |
| el resto | fondo de escala | con `LOG_UNIMP` diciéndolo |

**SUPUESTO y marcado como tal en el código**: el registro `ID` (`0x00`) devuelve
`0x00`. Ninguna fuente —Rockbox, iPodLinux, la cabecera de OpenMoko sacada del
datasheet— dice qué contesta un PCF50605 real. Lo único que el firmware
necesita es que **no sea `0x24`**, que es con lo que `0x283704` decide cambiarse
al mapa de registros de otro chip. Se elige el cero justamente porque no añade
información que no tenemos.

**Rockbox no ha regresado, pero sí ha cambiado**, y es un arreglo, no un daño:
ahora arranca en su menú principal en vez de entrar directamente en *Now
Playing*. `rtc_check_alarm_started()` de `rtc_pcf50605.c` decide que la alarma
despertó al aparato si el reloj y los registros de alarma coinciden dentro de
diez segundos. Sin RTC las dos lecturas devolvían lo mismo, así que Rockbox
creía que había sonado una alarma y arrancaba la reproducción **en todos los
arranques**. Con un reloj de verdad no coinciden y hace lo normal. Las capturas
de `assets/nav-baseline` están actualizadas; la navegación en sí no cambia (la
barra sigue moviéndose ~12 000 píxeles por paso de rueda y SELECT sigue
entrando).

#### 2. El bit 31: la vista sin caché de la DRAM

Pasado el PMU, el firmware daba un data abort leyendo `0x93EE1B70` —DRAM que
él mismo acababa de reservar, `0x13EE1B70`, con el bit 31 pegado— y lo imprimía
por semihosting en bucle.

Este muro **se explica solo**, no hay que inferirlo. Diez constructores de
buffer comparten un idioma: `0x22131c` y nueve del módulo de reserva
(`0x28c8fc`, `0x28c9dc`, `0x28cab4`, `0x28cb80`, `0x28cca8`, `0x28cd74`,
`0x28ce0c`, `0x28ced0`, `0x28d0ec`). Desde `0x28cebc`:

```
str  r0, [r4, #4]         ; el puntero alineado
ldrb r1, [r4, #12]        ; "el llamante dice que ya es sin caché"
cmp  r1, #0
bne  fin
mov  r1, #4
cmp  r1, r0, lsr #28      ; nibble alto 4, o sea IRAM?
beq  fin                  ; entonces no tocarlo
orr  r0, r0, #0x80000000  ; si no, así es como se usa el buffer
str  r0, [r4, #4]
...
bl   0x9aa18              ; sobre [ptr - holgura, ptr + tamaño + holgura]
```

**La excepción es lo que identifica el bit.** La IRAM es la única memoria de
este chip que nunca se cachea, y es exactamente la que el firmware no marca.
Rockbox dice lo mismo en C, con otra constante porque su vista cacheada vive en
otro sitio: `system-target.h:164`, `UNCACHED_ADDR(a) = a | UNCACHED_BASE_ADDR`.

Cómo se localizó, y el método vale la pena: **punto de ruptura en el `memcpy`
del firmware (`0xf0`) a través del gdbstub de QEMU**, preguntando quién pasa un
puntero de origen con el bit 31. Respuesta: `0x21701c`, una copia de 512 bytes
sacada de un buffer de bloque de disco de 32 KB cuyo descriptor, en
`0x13ee9b90`, guarda la reserva cruda en `+0` y esa misma dirección con el bit
31 en `+4`. Después, un **watchpoint de escritura** sobre esa palabra, que cae
justo en el constructor de arriba.

Como aquí no hay caché, cacheado y sin cachear son la misma memoria: un alias,
igual que el gemelo del bit 26 que ya estaba al lado.

#### 3. El Broadcom: el *handshake* de arranque

Con eso el retailOS llega al chip de vídeo y se queda en `0x2876dc` esperando
el bit 6 de `BCM_ALT_CONTROL`. Es la segunda mitad del arranque del Broadcom
que Rockbox también hace (`lcd-video.c:559-560`):

```c
while (BCM_ALT_CONTROL & 0x80);      /* esperar mientras está ocupado */
while (!(BCM_ALT_CONTROL & 0x40));   /* esperar a que esté listo */
```

Rockbox nunca llega ahí en esta máquina, porque `bcm_init()` solo corre si
`GPO32` bit 14 dice que el chip está sin alimentar y nosotros fingimos que lo
está. **El retailOS lo arranca él mismo en todos los arranques**, sin mirar ese
bit. Los dos bits nuevos se contestan como los dos que ya había: nunca ocupado,
siempre listo.

Y eso destapó dos errores del modelo del Broadcom que Rockbox nunca podía haber
enseñado:

- **Los registros de dirección son de 16 bits.** Apple los carga como dos
  medias palabras, la baja primero (`0x287c28`); Rockbox siempre escribe la
  palabra entera por `BCM_WR_ADDR32`. Asignar el valor en cada escritura dejaba
  solo la mitad alta, así que una subida de firmware a `0x000fc000` habría ido
  a `0x000f0000` sin decir nada.
- **El puerto de datos es tan ancho como el acceso.** Apple manda los últimos
  1..255 bytes de un bloque de dos en dos (`0x287ca8`); una escritura de media
  palabra que guardaba una palabra entera pisaba los dos bytes siguientes.

### Dónde se queda, con precisión

**La subida del firmware del Broadcom, que va por DMA.**

`0x287be8` parte la transferencia en un grueso y una cola, y le da el grueso a
`0x286fb4`, que vacía la caché sobre el origen, lo trocea en pedazos de 64 KB,
llama a `0x1599fc` por cada uno y **espera en un método virtual del `+0x18` del
objeto driver**. No hay nada modelado que conteste, así que la tarea se bloquea
para siempre y el planificador se queda en reposo — por eso la máquina parece
sana y no imprime nada.

Los registros que toca ya se ven en `-d unimp` como `pp502x.soc-regs`
`0x6000b000`, `0x6000b020`, `0x6000b040` y `0x6000b060`: **DMA0..DMA3** de
`pp5020.h`. Así que lo siguiente es el motor de DMA del PP502x, y los argumentos
de la llamada que se bloquea son conocidos:

```
bcm_load(name = 0x001affe8, data = 0x13ea136c, len = 0x312a0)
   -> 201 376 bytes a la SRAM del Broadcom, dirección 0
```

**Y hay que saberlo antes de ponerse**: aunque se modele el DMA, la pantalla no
se encenderá sola. Lo que se sube es el firmware del propio chip —el blob
`vmcs`, cuyo nombre de fichero está literalmente en la imagen, en `0x2878f4`:
`"VMCS    BIN"`— y después los dos guests **esperan a que ese firmware
conteste**:

| Espera | Apple | Rockbox |
|---|---|---|
| bit 0 de la palabra `0x10000C00` del chip | `0x287868` | `lcd-video.c:589` |
| `BCMA_COMMAND` (`0x1f8`) distinto de cero | `0x2878b0` | `lcd-video.c:594` |

Esas respuestas las produce código que no estamos ejecutando. O se emula lo
justo del Broadcom para fingirlas (y entonces hay que averiguar qué protocolo
de comandos espera el retailOS, que no tiene por qué ser el de Rockbox), o se
busca la ruta por la que Rockbox se salta el arranque del chip y se mira si el
retailOS tiene una equivalente.

## El firmware del Broadcom sube, y el chip contesta (2026-08-09)

Hecho lo de arriba: **el retailOS termina toda la puesta en marcha del chip de
vídeo**. Sube los 201 376 bytes del blob `vmcs` por DMA, arranca el VideoCore,
recibe las dos respuestas que esperaba, descubre la interfaz del chip y sigue.
Noventa segundos de ejecución con `-d guest_errors`: **el log sale vacío**. Ni
un pánico, ni un acceso rechazado, ni un registro sin modelar nuevo. Y el disco
trabaja de verdad — 460 `READ DMA EXT` de 64 sectores, unos 14 MB leídos, más
78 escrituras.

**La pantalla sigue negra.** Capturas a los 10, 20, 40, 60 y 90 s:
**0 píxeles encendidos en las cinco, 0,00 %**. Eso ya no es un fallo del
arranque; es que el retailOS no dibuja donde nuestra consola mira, y más abajo
está el porqué.

`navtest.sh` contra `assets/nav-baseline`: **sin regresión**. La barra sigue
bajando (12 312 y 12 114 px de diferencia por paso de rueda), SELECT sigue
entrando (31 597 px), y frente a la referencia guardada las capturas difieren
un par de cientos de píxeles (el reloj de la barra de estado) salvo la última,
que sale idéntica. *(La cifra exacta del reloj no es un invariante; ver la nota
al final de la sección "El retailOS dibuja, y se navega".)*

### Los cuatro muros de esta sesión

| # | Muro | Cómo se encontró | Arreglo |
|---|---|---|---|
| 1 | la tarea dormía para siempre tras pedir el DMA | ya estaba acotado | `pp502x-dma`, los **dos** bloques |
| 2 | `Invalid write at addr 0x0, size 1, region 'ipod-video.bcm2722'` | el propio log de QEMU, en cuanto el DMA empezó a mover datos | el puerto del BCM acepta accesos de un byte |
| 3 | bucles sobre el bit 0 de `0x10000C00` y sobre `BCMA_COMMAND` | 7,9 millones de lecturas de `0x10000c00` en `-d guest_errors` | el chip contesta como si su firmware corriera |
| 4 | data abort en `0xa0bd0` leyendo `0xEA00007A` | el firmware lo imprime por semihosting, y el PC salió parcheando `memory_region_access_valid` para volcar el estado del guest | publicar la interfaz del VideoCore en vez de un puntero nulo |

#### 1. El motor de DMA, y que son dos bloques

El aviso que traíamos —"los registros que toca ya se ven en `-d unimp` como
`0x6000b000/20/40/60`"— **era una pista falsa**. Esos son del bloque que
documenta `pp5020.h`, y aparecían solo porque una rutina de apagado del arranque
barre *todos* los canales de *los dos* bloques limpiando el bit 31.

La subida del Broadcom va por un **segundo bloque de DMA que `pp5020.h` no
menciona**: maestro en `0x60008000`, canales en `0x60009000`. `grep -rn
"60008000\|60009000" assets/rockbox/` da cero resultados; lo documenta
únicamente el firmware de Apple, que carga esa base en `0x1da198` y le cuelga
dos canales.

Los dos bloques son idénticos registro a registro. El mapa completo, con la
procedencia de cada bit, está en
[07-pp502x-hardware.md](07-pp502x-hardware.md).

**Los números de interrupción no son una conjetura, y esto importa** porque sin
la línea correcta el modelo mueve los datos y la tarea se queda dormida igual.
`0x1d98d0` registra los dos manejadores:

```
mov r1, #27 ; r2 = 0x2680c4 (b 0x1d9c40, manejador del bloque B) ; registrar
mov r1, #28 ; r2 = 0x2680c0 (b 0x1d9be0, manejador del bloque A) ; registrar
```

27 y 28 son índices de la tabla de interrupciones **del propio firmware**, no
líneas de hardware. La tabla se construye en `0x1fcaa0`, entrada *n* en
`objeto+0x208+4n`:

```
0x1fcc38   entrada 26 = -1 (no existe), entrada 27 = 26
0x1fcc44   entrada 28 = 27
```

O sea **bloque B → IRQ 26** y **bloque A → IRQ 27**. Que el índice 27 caiga
exactamente sobre el `DMA_IRQ = 26` que documenta Rockbox para el bloque que sí
conoce es la comprobación que hace fiable al par. Y la máquina en marcha está de
acuerdo: `CPU_INT_EN_STAT` (`0x60004020`) lee `0xcc002017`, con las líneas 26 y
27 habilitadas y la 28 no.

Una cosa **inferida** y hay que decirlo: que la dirección del periférico sea un
puerto y no un rango, o sea que solo `RAM_ADDR` avance. No lo dice ninguna
fuente; es la única lectura compatible con los dos usos reales, el FIFO del I²S
de Rockbox y el puerto de datos del Broadcom de Apple.

Y una que **no** se modela: las líneas de petición del periférico. Un canal con
`WAIT_REQ` puesto está esperando a que un periférico pida datos, y aquí no hay
ninguno que pida, así que se deja quieto. Eso es justo lo que mantiene la ruta
de audio de Rockbox comportándose igual que cuando estos registros eran un
`create_unimplemented_device`.

#### 2. El puerto del Broadcom acepta bytes sueltos

En cuanto el DMA empezó a mover datos, las cuatro transferencias fallaron en el
primer acceso. El descriptor que el retailOS programa para la subida deja el
campo de ancho de acceso a cero, y `pp5020.h:699` deletrea el cero como 8 bits.
Se comprobó leyendo el registro en marcha: `0x6000901c = 0x02000000`, campo
30:28 vacío.

Así que el retailOS sube su firmware de vídeo **byte a byte**. El modelo del
BCM tenía `valid.min_access_size = 2` y los rechazaba. Ahora acepta 1, 2 y 4, y
guarda exactamente lo que le dan: el contenido que acaba en la SRAM del chip es
el mismo se escriba como se escriba, porque el puerto avanza su puntero por el
tamaño del acceso.

#### 3. Las respuestas del chip, y de dónde sale cada una

Ninguna es un número puesto para que pase la comprobación. Las tres están
fijadas por el código que las interroga:

| Respuesta | Qué la fija |
|---|---|
| bit 0 de `0x10000C00` = 1 | los dos drivers prueban **ese bit y ninguno más** (`tst r0,#1` sobre un `ldrh` en `0x287880`, `& 1` en `lcd-video.c:589`) y luego escriben cero encima y no lo vuelven a leer. El resto de la palabra es libre |
| `BCMA_COMMAND` = **1**, exactamente | tres comprobaciones lo encierran: `bcm_load` gira hasta que sea distinto de cero (`0x2878b4`), su llamante lo trata como entero con signo y falla si es negativo (`0x1affc8`), y la rutina que busca la interfaz hace `cmp r0,#1 / bne <rendirse>` en `0x288080`. No queda ni un grado de libertad |
| `BCMA_STATUS` = base de la interfaz | `0x288088` la lee, `0x288090` rechaza la nula y `0x28809c` la no alineada |

El arranque del VideoCore es un **flanco, no un nivel**: escribir el bit 31 lo
suelta y nada lo para, porque el driver borra el registro entero dos escrituras
después (`lcd-video.c:591`) mientras el núcleo está demostrablemente en marcha,
y en `0x28805c` vuelve a mandar la secuencia de bootstrap por el puerto
esperando encontrarlo corriendo todavía. Esa segunda pasada es la que descarta
la idea de que el bootstrap resetee el chip.

Qué significan por separado los bits 31 y 30 del `0xC0000000`, y qué selecciona
la media palabra baja del timbre `0xA5A50002`, siguen **sin determinar**. El
modelo dispara con el bit 31 y con la magia `0xA5A5` respectivamente, que es lo
único que los drivers permiten deducir.

Tres cosas más que solo enseña el firmware de Apple:

- **Un comando cuenta solo si el puerto de escritura apuntaba al buzón.** Una
  subida de 200 KB pasea su puntero por encima de `0x1F8` sin que eso sea un
  comando — y en el chip real tampoco podría serlo, porque lo que pasa por
  encima es la propia imagen del VideoCore mientras el VideoCore está parado.
- **Las dos palabras de control se escriben como dos medias palabras**, así que
  el efecto hay que decidirlo sobre la palabra ya montada y no sobre un acceso.
  Actuar sobre el primero habría visto `0x0000` donde iba `0xC0000000`.
- **El puerto primario y el alterno son motores separados**, con sus propios
  punteros. Rockbox solo usa el primario; el retailOS usa los dos a la vez.

Y un error del modelo que Rockbox nunca podía haber enseñado: al terminar un
comando se escribía cero en `BCMA_STATUS`, un registro que **ningún guest lee**
— y precisamente el que el retailOS usa para sacar el puntero de la interfaz.
Ahora se sobrescribe `BCMA_COMMAND`, que es lo que hace el chip real y lo que
los dos drivers vigilan (`lcd-video.c:204-206` y `:500-506`). De rebote Rockbox
deja de repintar por la vía del *timeout* y pasa a su camino normal.

#### 4. El puntero nulo que el retailOS no tolera

Publicar "no hay interfaz" con un puntero nulo parecía lo honesto: es la
codificación que el propio driver define (`0x288090: cmp r1,#0 ; beq <error>`).
**No funciona**, y el fallo es instructivo.

`0x288058` devuelve error, sí. Pero **su único llamante, `0xcff6c`, no mira el
valor de retorno**: llama a los cuatro enganches de canal y termina con
`0xcff8c: b 0x28827c`, que desbloquea un mutex que solo se crea en el camino de
éxito (`0x2880dc`). Con el puntero nulo ese mutex no existe, `0x28827c` hace
`ldr r0,[r0]` sobre la dirección 0 —que tras el remap es DRAM, así que ni
siquiera falla ahí— se lleva `0xEA00007A`, la primera palabra de la propia
imagen del retailOS, y muere dereferenciándola en `0xa0bd0`.

Lo que se publica ahora es la estructura entera **con todos los bytes a cero**,
que el driver lee como "la interfaz está y no tiene canales": `0x2871d8` carga
el offset de un canal y `0x2871e0` se lo salta si vale cero. Es la ortografía
del propio driver para un canal ausente, y es lo honesto porque no ejecutamos el
VideoCore y detrás de un canal no habría nadie.

**Marcado como SUPUESTO en el código**: dónde se pone esa estructura
(`0x00180000` de la memoria del chip). La dirección real la elige el firmware
`vmcs`, que no corremos; nada depende del número, solo de que esté dentro de la
memoria del chip, alineada a 4, y lejos tanto de la imagen que se sube (0 a
`0x312a0`) como del framebuffer de Rockbox (`0xE0000`).

### Cómo se localizó el muro 4, que es método reutilizable

El semihosting dio los registros del abort pero no el PC. El truco: **parchear
temporalmente `memory_region_access_valid()` en `system/memory.c`** para que,
además del "Invalid read at addr", llame a `cpu_dump_state(current_cpu, stderr, 0)`.
Sale el estado completo del guest en el instante del acceso rechazado:

```
R00=ea00007a R13=108c6f24 R14=002893f0 R15=000a0bd0
```

`R15` es el PC del fallo y `R14` la dirección de retorno, que da el llamante de
un tirón. Comprobación cruzada gratis: el manejador de abort del firmware
imprimía `R14 : 000A0BD8`, que es exactamente `PC + 8` — el `R14_abt` del ARM.
El parche está revertido; se reaplica en diez segundos cuando haga falta.

### Dónde se queda ahora, con precisión

El retailOS está **vivo y trabajando**: estado del BCM en `0x108cbec8` = 3
("hilos del VideoCore en marcha"), el mutex de la interfaz creado en
`[0x10831978]`, la base publicada en `[0x10831980] = 0x00180000`, y la tabla de
offsets de canal espejada en `0x108d3bd4` con los ocho a cero, que es lo que le
dimos.

**El siguiente muro es la interfaz de host del VideoCore, y es de otra clase.**
No es un bit que falte: es que en este aparato **la UI la pinta el chip de
vídeo**, no el PP502x. Las pruebas, todas negativas y todas verificadas:

- `findlit.py` no encuentra **ni una** instrucción que cargue `0x000E0000`
  (`BCMA_CMDPARAM`, donde Rockbox escribe los píxeles) en siete megabytes.
- Tampoco ninguna que cargue `0xFFFF0000` (`BCMCMD_LCD_UPDATE`) desde el driver
  del BCM. La única escritura a `BCMA_COMMAND` de toda la imagen es la del
  bootstrap, con valor 0.
- El controlador LCD propio del PP502x (`0x70003000`) tiene **una sola
  referencia** en la imagen, en la rutina de apagado de periféricos del arranque
  (`0x1328`). No hay driver.

Lo que hay en su lugar: cuatro canales de anillo sobre la memoria del chip, y el
VideoCore **tirando** de la memoria del host. `0xf9ca0` (la tarea `VCHR`) atiende
peticiones y despacha por opcode ASCII: `'H'` (`0xf9ff0`) copia plana
host→BCM, `'Y'` (`0xfa110`) blit 2D con escalado en coma fija 16.16, `']'`
(`0xfa1b4`) barrido por líneas. **En los tres casos la dirección de origen en la
memoria del host viene en el mensaje del VideoCore**, no la elige el PP.

Así que para ver píxeles habría que montar un "VideoCore falso" que hable el
protocolo de anillo *y* pida los datos. Lo primero es derivable del consumidor;
lo segundo exige saber qué direcciones pedir.

### La pista más acotada que dejamos

El análisis anterior daba por no localizado el escritor de `[0x1083198C]`, el
callback que traduce índice de línea a dirección del host. **Está localizado.**
`findlit.py` sobre `1083198c` da exactamente una instrucción, `0x1c29c4`, y es
un inicializador estático de C++ con el guardián `tst r0,#1` del ABI de ARM:

```
1c299c: ldr r0,[0x108315c8] ; ldr r0,[r0,#4] ; tst r0,#1 ; bne fin   <- guardián
1c29c0: ldr r0, =0x001c2a04     ; el callback
1c29c4: ldr r1, =0x1083198c     ; la ranura
1c29cc: str r0, [r1]            ; *** aquí se registra ***
```

y el callback `0x1c2a04` no es un cálculo, es **una búsqueda en una tabla de
direcciones por línea** que cuelga de `[0x108315c8 + 8]`:

```
1c2a10: ldr r4,[r0,#8]      ; el contenedor; si es nulo devuelve 0
1c2a28: bl 0x29e034         ; iterador: primera entrada
1c2a3c: bl 0x29e034         ; ultima entrada
1c2a54: bl 0x29e034         ; entrada de la linea pedida, luego se acota
1c2a80: ldr r0,[r0]         ; *** la direccion del host de esa linea ***
```

Quien rellena `[0x108315c8 + 8]` es `0x1c2a90`, y solo lo llaman dos sitios,
`0x1c7c30` y `0x1c7cd4`, los dos dentro de la misma rutina — y esa rutina manda
comandos ASCII cuyas cadenas están doce bytes más abajo: `"transitions"`,
`"push"`, `"alphawipe"`. Es decir, **el callback de líneas es solo para las
transiciones de pantalla**, y se registra justo antes y se borra (`mov r1,#0 ; b
0x1c2a90`) justo después.

Con eso, el siguiente paso queda acotado a una pregunta concreta y contestable:
**poner un punto de ruptura en `0x1c7c30` con el gdbstub y leer el contenedor
que pasa en `r8`** da, de una sola vez, dónde vive el framebuffer de la UI en la
DRAM del host y con qué paso de línea. Con eso ya se puede decidir si merece la
pena montar el anillo o si basta con apuntar la consola de QEMU a esa memoria.
Lo que no se sabrá por ahí es el formato de los mensajes `'H'`/`'Y'`, que hay que
sacar de la tabla de saltos de `0xf9e2c` (30 entradas, cinco identificadas).

**Advertencia sobre ese punto de ruptura**: hoy no se alcanza. `0x1c299c` es un
inicializador estático perezoso y `[0x1083198C]` sigue valiendo cero en marcha,
igual que `[0x108315c8]`. La rutina de transición solo corre cuando hay pantalla,
y no la hay. O sea que primero hace falta que arranque la UI, y para eso hace
falta el canal.

### Dos cosas que se vieron volcando la DRAM, y que cambian el panorama

Con `pmemsave 0x10000000 0x4000000` a los 55 s y buscando cadenas:

**Las cuatro tareas del VideoCore existen y corren.** `VCIH`, `VCFS`, `VCHR` y
`VCPD` aparecen no solo como literales de la imagen sino **copiados al montón**
(`0x13e5d9cc`, `0x13e5ef2c`, `0x13e5fd78`, `0x13e60e58`…), que es donde el
planificador guarda el nombre de cada tarea. El retailOS llegó hasta el final de
`0x2892d4`.

**Y la que no corrió es la de la pantalla.** `display_control` y `transitions`
aparecen **solo en direcciones de la imagen** (`0x10163c40`, `0x101c7cd8`); no
hay ni una copia formateada en el montón. Es decir, `0x164e9c` nunca llegó a
componer su `display_control %d model=%d power=%d`. Sin canal no hay a quién
mandárselo, y sin eso la UI no arranca. Es prueba indirecta —un buffer de pila
pudo haberse pisado— pero apunta en la misma dirección que todo lo demás.

**El `vmcs.bin` que subimos es el de verdad.** Esto no se sabía. El buffer que
el DMA lee (`0x13ea136c`, `0x312a0` bytes) contiene, en su offset `0xbfe8`, la
tabla de símbolos del firmware del VideoCore:

```
tasks, version, commands, power_down, set_vll_dir, led_control, disk_notify,
usb_inserted, audio_enable, inuse_notify, power_control, audio_control,
motor_control, camera_control, display_control, end_application,
power_management, load_application, vmcs_display_clear, vmcs_display_enable
```

más plantillas de **respuesta** —`value=%d`, `drive=%i inuse=%i`,
`error=%d error_msg="bad argument"`— que solo tienen sentido en el lado del
chip. Y viene de `assets/ipod-video-disk.img`, offset de fichero `0xb27508`, que
es la partición de firmware; el mismo contenido está en
`assets/ipod-video-fw.bin` en el mismo offset. **No** está en `osos_full.bin`.

O sea: la máquina emulada ya tiene el firmware real del VideoCore, lo lee del
disco, lo sube entero y correcto a la SRAM del chip modelado. Lo único que no
hace es ejecutarlo. Eso convierte "emular el VideoCore" de idea abstracta en una
opción con material: el binario está ahí, con símbolos, y el vocabulario de
comandos que el host le manda está a la vista.

## El retailOS dibuja, y se navega (2026-08-09)

**La interfaz de Apple está en pantalla y responde a la rueda.** Sin ejecutar
una sola instrucción de VideoCore: lo que se ha modelado es su *protocolo*.

| Fotograma | Qué se ve |
|---|---|
| 10 s en adelante | la pantalla de idioma del primer arranque: `Language`, `English`, `日本語`, `Čeština`, `Dansk`, `Deutsch`, `Español`, `Français`, `Ελληνικά`, `Italiano` |
| SELECT | el **menú principal del iPod** en el idioma elegido: Music / Photos / Videos / Extras / Settings / Shuffle Songs |
| rueda | la barra azul baja de *Music* a *Videos*, 15 360 px de diferencia |
| SELECT | entra en *Videos*: Video Playlists / Movies / Music Videos / Video Podcasts / Video Settings, 18 731 px |
| MENU | vuelve al menú principal con *Videos* aún seleccionado, otros 18 731 px |

Capturas en `assets/apple/`, reproducibles con
`tools/ipod_video/appletest.sh`. Las seis del arranque (10, 20, 40, 60, 90 y
120 s) dan **75 267 de 76 800 píxeles encendidos, el 98,00 %, y 307 colores**,
idénticas entre sí — la pantalla se pinta y se queda.

`navtest.sh` contra `assets/nav-baseline`: **sin regresión**. Seis fotogramas
difieren un par de cientos de píxeles (el reloj de la barra de estado) y el
séptimo **0**, byte a byte idéntico.

> **Corregido el 2026-08-09, ejecutándolo.** Aquí ponía que el séptimo
> fotograma difiere 1 590 px "por los contadores de la pantalla de grabación".
> Medido hoy: el séptimo es *Now Playing*, no lleva reloj, y sale **byte a
> byte idéntico** a la línea base. El propio mensaje del commit `5b5af8966` ya
> decía "0 in the seventh".
>
> Y sobre los otros seis: **la cifra exacta no es un invariante**. En este
> documento aparece como 120 y como 225–227, en el commit `5b5af8966` como 201,
> y una ejecución del 2026-08-09 dio 266. Todas son el reloj, y cambian con los
> dígitos que cambien entre una captura y otra. Lo estable —y lo único que hay
> que comprobar— es **la forma**: seis fotogramas con un par de cientos de
> píxeles de diferencia y el séptimo a cero.

### El hallazgo que da la vuelta al diagnóstico anterior

La sesión anterior concluyó que la UI la pinta el VideoCore *tirando* de la
memoria del host, y que por eso hacía falta un VideoCore falso que además
supiera qué direcciones pedir. **La primera mitad es cierta y la segunda no.**

Esa ruta —los opcodes `'H'`, `'Y'`, `']'` de la tarea VCHR, y el callback de
líneas de `[0x1083198C]`— existe, pero es la del **pase de diapositivas y las
transiciones**: `[0x1083198C]` solo lo rellena `0x1c2a90`, y a `0x1c2a90` solo
lo llaman `0x1c7c30` y `0x1c7cd4`, los dos dentro de la rutina de transiciones.

La UI normal va por el camino contrario, y **es el host quien empuja**:

1. `0x1649ac` pide al VideoCore que le cree **dos** superficies (doble búfer en
   el chip, un solo búfer de píxeles en el host).
2. Cada una es un mensaje de **opcode 8** por el canal de tipo 2 que lleva
   formato, ancho, alto y paso, y una dirección de chip a cero, que en este
   protocolo significa *"elígela tú"* (`0x286d50` guarda la que devuelve la
   respuesta solo si el llamante puso cero).
3. Como el VideoCore somos nosotros, **la dirección la elegimos nosotros**.
4. `0x164524` sube la superficie entera por el **puerto alterno** y `0x165024`
   sube después solo la banda sucia.
5. **Opcode 3** presenta: asa en `+12`, ancho en `+24` y alto en `+26`.

Es decir: no hay que adivinar dónde está el framebuffer de Apple ni pedir nada.
En cuanto contestamos al opcode 8, el retailOS mete su interfaz dentro de la
memoria del chip modelado, y la consola de QEMU no tiene más que apuntar ahí.

Confirmación independiente, y muy útil para futuras sesiones: el retailOS ya
componía su UI entera en la DRAM del host **antes** de todo esto. El descriptor
estático de `0x10883634` la sitúa en `0x10885db0`, 320×240, RGB565, paso 640, y
un volcado de esa memoria a los 75 s ya enseñaba la pantalla de idioma
completa. Lo que faltaba no era el dibujo: era la entrega.

### Los cuatro muros de esta sesión

| # | Muro | Cómo se encontró | Arreglo |
|---|---|---|---|
| 1 | los ocho offsets de canal a cero, o sea "no hay canales" | ya estaba acotado | publicar canales de verdad, tipos 1 y 2 |
| 2 | sin canal, cada orden ASCII se descarta **sin formatear** | `0xe9380` devuelve −1 antes del `vsnprintf` de `0xe9394` | lo anterior |
| 3 | ninguna interrupción despierta al despachador del VideoCore | `[0x1081eca8]` vale cero toda la ejecución y ninguna instrucción lo escribe | **no hace falta**: ver abajo |
| 4 | la rueda no movía nada, con Rockbox funcionando en la misma máquina | el despachador de interrupciones de Apple, `0x277128` | conducir `HI_IRQ`, la línea 30 del banco bajo |

#### 1 y 2. Los canales

El protocolo entero —cabecera de dieciséis bytes con magia `0xF1A55A1F`, dos
anillos por canal, cuatro punteros cada uno en su propia línea de dieciséis
bytes, y el timbre que es la media palabra `0x0031` a `BCM_CONTROL`— está
documentado campo a campo, con la instrucción que fija cada uno, en
[07-pp502x-hardware.md](07-pp502x-hardware.md). El modelo está en
`hw/arm/ipod_video/ipod_video_vc.c`.

Lo que se publica son dos canales: **tipo 1** (órdenes de texto) y **tipo 2**
(RPC binario de gráficos). Los tipos 5, 6 y 7 se dejan **ausentes a propósito**:
son la dirección en la que el VideoCore habla primero, y eso es justo lo que no
se puede modelar (muro 3). Dejarlos a cero es la ortografía del propio driver
para un canal que no existe, y `0xcff6c` no mira ni un valor de retorno.

Las respuestas de texto salen del propio `vmcs.bin`: los veinte nombres de orden
que registra con `gencmd_register` están en un bloque contiguo del offset
`0xBE24`, y las plantillas de respuesta en `0xBFF8` (`value=%d`) y `0xC004` en
adelante (`error=%d error_msg="…"`). Una orden registrada recibe **la cadena
vacía**, que es lo honesto para un *setter* y lo único que no puede ser un
número equivocado; cualquier otra recibe la plantilla "no registrada" del propio
firmware. El único requisito duro es que la carga útil **no** sea de longitud
cero: `0x2872f0` devuelve éxito solo si volvió al menos un byte.

Las cinco órdenes que manda el arranque, tal como las ve el modelo:

```
set_vll_dir /Resources/VideoCore/Library
display_control 0 freeze=0
display_control 0 model=1 power=-2
power_management set_policy manual 27
pm_set_policy min
```

**Corrección a la nota anterior**: la roadmap decía que `0x164e9c` "nunca llegó
a componer su `display_control`". Era correcto, pero no por lo que se pensaba.
La llamada sí se hace; lo que pasa es que `0xe9358` comprueba el canal en
`0xe9374` y aborta en `0xe9380`, **antes** del `vsnprintf` de `0xe9394`, así que
la cadena formateada nunca llega a existir en el montón. La prueba indirecta del
volcado de DRAM era buena; la causa que se le atribuyó, no.

#### 3. La interrupción que no hizo falta

El despachador que despertaría a una tarea dormida, `0x288100`, solo corre desde
la tarea VCIH (`0x1239b8`), que espera un evento en `[0x1081eca8]`. `findlit.py`
da **una sola** instrucción que carga ese literal y es una lectura; un
*watchpoint* de escritura durante todo el arranque solo pilla el `memset` del
*scatter-load*. Sin la línea de interrupción del BCM —que sigue **sin
identificar**— esa tarea no despierta jamás.

Y resulta que para el camino que dibuja **no importa**. Todas las peticiones del
retailOS son síncronas dentro de una misma tarea: `0x28861c` para mandar y
`0x2883d4` justo después para leer. Y `0x2883d4` solo se duerme si el anillo
está vacío — `0x2883f8` vuelve en cuanto tiene lo que pidió, y antes de esperar
`0x288418` refresca el puntero de escritura desde la memoria del chip. Así que
**si la respuesta ya está puesta cuando el guest mira, `0x2870dc` no se alcanza
nunca**. El modelo actúa sobre el propio `str` del puntero de escritura del
guest, que ocurre antes, así que siempre llega primero.

Eso convierte "hace falta identificar la IRQ del BCM" de bloqueo estructural en
deuda acotada: solo hace falta para los canales 5 y 6, o sea para el sistema de
ficheros del VideoCore y para las transiciones.

#### 4. `HI_IRQ`: el muro que solo podía enseñar Apple

Con la pantalla pintada, la rueda no movía nada — y Rockbox se navegaba entero
en la misma máquina y con el mismo modelo de rueda. La causa no estaba en la
rueda.

`pp5020.h` de Rockbox lista `HI_IRQ` como la fuente **30** del banco bajo, entre
`DMA_IRQ` 26 y `GPIO0_IRQ` 32, y luego **no la lee nunca**: su manejador va
directo a `CPU_HI_INT_STAT`. Con solo Rockbox delante, dejar esa línea sin
conducir parece correcto.

El despachador del retailOS, `0x277128`, hace lo contrario: lee las dos palabras
de estado y se niega a mirar el banco alto si el bajo no trae el bit 30.

```
277134:  ldr r4, [0x60004000]     ; banco bajo
277138:  ldr r5, [0x60004100]     ; banco alto
2771a4:  tst r4, #0x40000000      ; hay algo arriba?
2771a8:  beq  <espuria>
2771dc:  tst r5, #0x100           ; la linea I2C, o sea el click wheel
2771e4:  bl 0x281350              ; el manejador del click wheel
```

O sea que **cada pulsación se tomaba y la tiraba el propio guest**. El arreglo
es una línea de lógica en el controlador: el banco alto conduce la línea 30 del
bajo.

De rebote quedó verificado que el retailOS usa **el mismo formato de paquete de
rueda** que Rockbox (`0x281374: cmp ip, #0x8000001a`), lo cual es una
confirmación independiente de algo que hasta ahora solo decía una fuente. Los
detalles están en [07-pp502x-hardware.md](07-pp502x-hardware.md).

### Lo que se elige y no se deriva, marcado como tal

- **Dónde vive la interfaz** en la memoria del chip (`0x00180000`), dónde van
  los bloques de control y los anillos dentro de ella, y **dónde van las
  superficies** (`0x00200000` en adelante). La dirección real la elegiría el
  firmware `vmcs`, que no corremos; nada depende del número.
- **Que toda orden registrada tenga éxito.** No somos el VideoCore y no podemos
  fallar por sus motivos.
- **El código de resultado 0** en la cabecera de respuesta, y el cero que
  devuelven los opcodes 1, 2, 4, 9 y 16 del canal de gráficos. Ninguno de los
  llamantes ramifica sobre ellos.
- La memoria del chip pasa de 2 a **4 MB** para que quepan las superficies. El
  BCM2722 real tiene su propia SDRAM para eso; cuánta, sin determinar.

### Lo que sigue sin estar

- **La línea de interrupción del BCM.** Sin ella no hay canal 5 (sistema de
  ficheros del VideoCore) ni canal 6 (VCHR), y por tanto no hay transiciones ni
  pase de diapositivas ni reproducción de vídeo.
- **Los canales de tipo 7** y dieciocho de los treinta opcodes de VCHR.
- **El campo `+0x00`** del bloque de control de canal y el `u16 +0x0e` de la
  cabecera de mensaje: siempre cero, nadie los lee.
- Qué contesta de verdad el firmware a cada `display_control`. Da igual hoy —
  `0x164eec` solo mira el retorno— pero es una suposición.
- Audio, y el COP.

## Estrategia (iPod classic 6G)

El `osos` de Apple está cifrado con la clave GID del SoC
(ver [01-firmware-ipsw.md](01-firmware-ipsw.md)), así que no es un objetivo
inicial razonable. **El primer objetivo es arrancar Rockbox**, que es open
source, se compila sin firmar, tiene drivers a nivel de registro para todo el
S5L8702 y se carga en `0x08000000` — exactamente igual que el `osos`.

Ventaja de rebote: los drivers de Rockbox son la especificación ejecutable
contra la que validar cada periférico que modelemos.

## Hitos

> **Estos hitos son los del iPod classic 6G (S5L8702)**, que fue el objetivo
> original. H3 en adelante están parados: no por falta de plan, sino porque el
> firmware del Classic está cifrado y no hay nada real que arrancar. El trabajo
> que sí llegó a alguna parte es el del iPod Video, y no sigue esta numeración
> — su recorrido es todo lo anterior de este documento.

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

## Estado actual (2026-08-09)

> **Corregido.** Aquí ponía *"En H0 completo, empezando H1"*, que era la última
> línea del documento y llevaba unos 50 commits sin tocarse. Contradecía a la
> tabla de hitos de este mismo fichero (H0, H1 y H2 están ✅) y a la tabla de
> estado de la cabecera.

**iPod Video 5G (`iPod-Video`, PP5022C)** — es la máquina que arranca software
real, y va muy por delante de lo que describen los hitos H0–H7:

- **Rockbox** arranca hasta el menú y se navega con la rueda.
- **El retailOS de Apple** arranca, dibuja su interfaz y se navega: pantalla de
  idioma → menú principal → submenú de *Videos* → atrás.
- Lo que falta: **audio** (ni I²S ni códec), **USB**, y ejecutar de verdad el
  firmware del VideoCore, del que hoy sólo se modela el protocolo.

**iPod classic 6G (`iPod-Classic`, S5L8702)** — H0, H1 y H2 completos: ejecuta
código, con timers y una ruta de interrupción verificados de extremo a extremo
contra tests bare-metal. **No arranca firmware real** y no lo hará mientras el
`osos` siga cifrado con una clave que vive en el silicio. H3–H7 siguen abiertos
y son reaprovechables si aparece un `osos` descifrado; ver también
[08-s5l8702-fuentes-externas.md](08-s5l8702-fuentes-externas.md), porque
`freemyipod/qemu` ya tiene un modelo del S5L8702 bastante más completo que el
nuestro.
