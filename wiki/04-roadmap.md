# Roadmap

## Dos objetivos, dos máquinas

El proyecto arrancó apuntando al **iPod classic 6G** (S5L8702) y se topó con que
su firmware está cifrado con una clave que vive en el silicio. Al aparecer un
**iPod Video 5G** físico, cuyo firmware está en claro, el objetivo de arrancar
el sistema original de Apple se movió allí.

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Classic` | S5L8702 | ejecuta código, timers e interrupciones verificados |
| `iPod-Video` | PP5022C | **Rockbox se navega entero; el retailOS de Apple arranca del todo y se queda subiendo el firmware del chip de vídeo por DMA** |

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
