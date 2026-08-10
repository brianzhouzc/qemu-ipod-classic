# El retailOS del iPod classic 6G arrancando en QEMU

Qué hace el `osos` de Apple cuando se le deja correr en la máquina
`iPod-Classic`, en qué orden pide las cosas, y dónde se para hoy.

Todo lo que sigue está **medido** salvo donde diga `INFERIDO` (razonado a
partir de algo verificado) o `INDETERMINADO` (no lo sabemos y no fingimos
saberlo). Las trazas son reproducibles con las órdenes del final.

---

## 0. Estado

| Hito | Estado |
|---|---|
| 1. Se carga y ejecuta la primera instrucción | **hecho** |
| 2. Pasa la inicialización temprana sin colgarse | **hecho** |
| 3. Inicializa periféricos y llega a su bucle principal | **hecho** — kernel RTXC arrancado, planificador vivo, tarea idle en `0x22003b1c` |
| 4. Dibuja algo en pantalla | **no** — pero ahora se sabe exactamente qué falta: §9 |

> **Agosto de 2026.** La máquina ya tiene panel: `ipod_classic_lcd.c` modela una
> memoria de imagen de 320×240 RGB565 con ventana y las órdenes `0x2A/0x2B/0x2C`,
> y la expone por `qemu_console`, así que un `screendump` mide de verdad si hay
> dibujo. Validado con un caso de control (`tools/ipod_classic/lcdtest.S`):
> 76 800 de 76 800 píxeles encendidos y 1920 colores distintos, exactamente la
> rampa que escribe el test. Con el retailOS, **con disco y sin él, el
> `screendump` sigue dando 0 píxeles encendidos**. La pantalla negra ya no es
> "no lo sabemos": es una medida.

Con `-drive if=ide` el arranque es limpio: cero excepciones, cero reinicios.
Sin disco entra en un ciclo de caída y reinicio (§4).

Reproducido en auditoría independiente (agosto de 2026), con estas cifras:
con disco, 35 s de `-d int` dan 27 166 `Taking exception 5 [IRQ]` y **nada
más**; `PC = 0x22003b20`, `SP = 0x22010b70`, modo SVC; `0x22003b1c` es
`mcr p15,0,r0,c7,c0,4` y `0x22003b30` es `b 0x22003b0c`, comprobado en el
desensamblado. Sin disco, 25 s dan 23 *Data Abort*, 23 llamadas de semihosting
y 23 `SWRCON`. El DMA de ATA entrega de verdad: con una imagen que lleva
`55 aa` en `0x1FE`, `pmemsave 0x0be5d3e0` la encuentra ahí después del
arranque.

Y la advertencia que va con el hito 3: **«llegar al bucle idle» no es
«arrancar»**. Todas las tareas de aplicación siguen bloqueadas, no se dibuja
nada, y el camino *sin* disco llega más lejos en la interfaz antes de morirse
(§4). Un sistema quieto no es necesariamente un sistema que funciona.

---

## 1. La imagen tiene dos regiones de enlace

`osos_decrypted.bin` no es un binario plano. Es una imagen con carga dispersa
(*scatter-load*) de ARM ADS/RVCT con **dos** bases:

```
fichero [0x00000, 0x0AE48)  ->  0x22000000   stub + núcleo de arranque (IRAM0)
fichero [0x0AE48, fin)      ->  0x08000000   el cuerpo del SO (DRAM)
```

En la práctica: `direccion = 0x08000000 + (offset_fichero - 0xAE48)` para todo
lo que esté a partir de `0x0AE48`. Desensamblar la imagen entera con una sola
base apunta a la instrucción equivocada en el 99 % del fichero.

Para desensamblar:

```bash
# la parte de IRAM
arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x22000000 \
    --start-address=0x22003b0c --stop-address=0x22003b40 osos_decrypted.bin
# el cuerpo en DRAM
arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x07FF51B8 \
    --start-address=0x08327760 --stop-address=0x08327780 osos_decrypted.bin
```

`0x07FF51B8` no es un número mágico: es `0x08000000 - 0xAE48`.

---

## 2. El bloque sin nombre en `0x3C100000`

**Ninguna fuente que tengamos lo documenta.** El único `0x3C100000` de
`s5l87xx.h` de Rockbox es `LCD_BASE`, y está bajo
`#if CONFIG_CPU==S5L8700` — otro chip. El modelo de freemyipod tampoco tiene
nada ahí. La única autoridad es el código de Apple.

Y es pequeño: buscando la palabra literal `0x3C100000` en la imagen y después
todos los `LDR` relativos a PC que resuelven a una de esas palabras salen
**exactamente tres sitios**, así que la lista es completa, no "lo que
apareció":

| Sitio | Qué hace |
|---|---|
| `0x08031d9c` | `str r0,[r1,#8]` — semilla: siete bytes de estado + `TECNT` + la palabra de `0x3C700008` |
| `0x08327738` | `str #0,[r1]` — escribe **cero** en `+0` |
| `0x08327760` | `while (!([+0] & 7)) ; return [+4]` |

> **Corrección (auditoría).** El tercer sumando **no** es «la semilla
> anterior», como decía la primera versión de esta página. El literal de
> `0x08031de0` es `0x3C700000`, o sea `TIMER_BASE`, y `+0x08` es `TADATA0`.
> La semilla se mezcla con **dos** lecturas de temporizador independientes, y
> nadie vuelve a leer nunca `+4` ni `+8` de este bloque: el registro de
> semilla es de sólo escritura desde el punto de vista del huésped.

En contexto: `0x08327714` toma una sección crítica y escribe ese cero;
`0x08031d30` siembra `+8`, saca veinte palabras por `0x08327760`
descartando diecinueve, y suelta la sección en `0x08327750`. Sembrar un
generador y tirar sus primeras salidas es un idiotismo de RNG, y la semilla se
mezcla con un contador libre (`0x08015748` es un *veneer* a `0x22001EC4`, que
es "devuelve `TECNT`"). Nada más encaja: entre las veinte lecturas no entra
ningún dato nuevo, así que un checksum o un hash devolvería la misma palabra
veinte veces y el bucle no tendría sentido.

- Que sea un **RNG**: `INFERIDO`.
- El contrato de tres registros: **verificado**.
- Cómo se llama y qué más tiene: `INDETERMINADO`.

### La trampa: no hay bit de habilitación

Ninguna instrucción escribe un valor distinto de cero en `+0`. La única
escritura es el cero de `0x08327738`, y ocurre **antes** de las veinte
lecturas, no después. Un modelo que condicione "hay dato listo" a que el
huésped haya habilitado el bloque se cuelga en la primera extracción, porque
`0x08327760` gira sin *timeout* y sin salida. Los bits de listo tienen que
subir solos.

Es el patrón 1 de [09-historia-5g.md](09-historia-5g.md) — *el bit de estado
que nunca llega* — con un agravante: aquí el bit parecía tener un habilitador
y no lo tiene.

### Cuánto pesa de verdad este bloque (medido en la auditoría)

**Con disco enganchado, el retailOS no toca `0x3C100000` ni una vez**:
`0x08327760` no aparece en un log de `-d in_asm` de ese arranque. El bloque
sólo se alcanza en el camino **sin** disco, una vez por intento de arranque, y
el sitio de siembra `0x08031d9c` no se ejecuta en ninguno de los dos — así que
el estado del generador arranca siempre de cero.

O sea: quitar el falso bit de habilitación desbloqueó de verdad un giro
infinito **en el estado del árbol de aquel momento**, pero el bloque no está
en el camino que hoy lleva al bucle idle. No se le puede acreditar el hito 3.

---

## 3. La interfaz de LCD en `0x38300000`

Es un puerto de órdenes tipo MPU/8080, **no** un motor de barrido: no hay
dirección de framebuffer ni temporización de vídeo. El panel guarda su propia
imagen. El bloque de `0x39200000` (`LCDBASE`, "CLCD CONTROLLER" en Rockbox) sí
tiene direcciones de framebuffer, y el retailOS apenas lo toca.

Los bits de estado (`LCD_STATUS`, `+0x1C`) son lo que importa, porque cada uno
es un giro sin *timeout* por los dos lados. **Rockbox y el retailOS coinciden
en los tres**, que es lo que los hace fiables:

| Bit | Significado | Rockbox | retailOS |
|---|---|---|---|
| 0 | hay dato de lectura en `LCD_DBUFF` | `lcd-s5l8702.c:241` | — |
| 1 | interfaz ociosa, se puede escribir control | `lcd-s5l8702.c:207` | `0x080b6f98`, `0x080b6ffc` |
| 4 | FIFO de escritura ocupada | `lcd-s5l8702.c:215` | `0x080a90b4` |

`s5l87xx.h` anota el valor de reset como `0x0106`: bits 1, 2 y 8 puestos y el 4
limpio — ocioso y no ocupado, coherente con lo anterior. Qué significan los
bits 2 y 8 es `INDETERMINADO`; ningún driver los mira.

Con la ventana sin implementar, los `while (!(LCD_STATUS & 0x2))` no vuelven
nunca. Es el muro que se comió 48 millones de lecturas en una traza de 45 s.

**El bloque es más ancho de lo que Rockbox sabe.** El retailOS escribe
`0x3830007C = 0x804` y `0x38300088 = 0x1000000` en su primerísima configuración
de LCD, antes de tocar nada que Rockbox nombre. Los dos son `INDETERMINADO`,
pero la ventana tiene que cubrirlos o se caen al suelo de E/S de la máquina.

### Por dónde pasan los píxeles: **cero llegan**, y ahora está contado

Corrección a las dos versiones anteriores de esta sección. Con el panel
modelado y `LCDTRACE=1` contando cada acceso, un arranque sin disco de 25 s da:

```
LCDTRACE totals: 63 writes to the command port +0x04, 168 to the data port
                 +0x40..0x5C, of which 0 were pixels; 21 frame-engine starts
```

Las 168 escrituras al puerto de datos son **21 arranques × 8 parámetros**: los
cuatro bytes de `0x2A` (`00 00 01 3f`) y los cuatro de `0x2B` (`00 00 00 ef`).
**Ni una sola palabra de píxel.** La frase «llega una palabra de píxel por
escritura de CPU» era ese byte `0x3f`, y la conclusión «luego el framebuffer va
por DMA» no se sostiene por otro camino: en un arranque sin disco entero,
**ningún canal de DMAC0 apunta jamás a `LCD_WDATA`** (`-d unimp`: el único
destino programado es `0x3CA00010`, el FIFO de I²S, y el LLI que se escribe es
`0x22010020`, también de audio).

Lo que sí hace el retailOS es arrancar un **motor de trama** dentro del propio
bloque de LCD, cuyos registros no conoce ninguna fuente externa:

| Registro | Qué se le escribe | Sitio |
|---|---|---|
| `+0x70` | `1` — arrancar | `0x08127f94` |
| `+0x74` | `(320 << 16) \| 240` | `0x08127f8c` |
| `+0x78` | `0x0005000a` `INDETERMINADO` | `0x08127f3c` |
| `+0x7C` | `0x00000804` `INDETERMINADO` | `0x080afb1c` |
| `+0x80` | `1` … `0` alrededor de fijar la ventana | `0x0808b000`/`0x0808b030` |
| `+0x88` | `0x01000000` `INDETERMINADO` | `0x080afb0c` |
| `+0x8C` | `while (LCD[0x8c] & 3);` — bits [1:0] = transferencia en curso, y es un giro **sin timeout** por partida doble | `0x080a4090`, `0x080b0128` |

**De dónde lee ese motor es `INDETERMINADO`**: no hay ningún registro de
dirección de origen en los 256 bytes de la ventana, y no hay DMA apuntándole.
El modelo lo acepta, lo cuenta y declara `+0x8C` libre, que es lo que ya hacía
la ventana sin implementar; la diferencia es que ahora lo dice en voz alta.

### El panel, que sí está modelado

`ipod_classic_lcd.c` lleva una memoria de 320×240 en RGB565, ventana con
`0x2A`/`0x2B`, puntero de escritura tras `0x2C`, y un `qemu_console`. Todos los
números vienen de una medida, ninguno de un datasheet:

- **Juego de órdenes de 8 bits**: el huésped escribe `LCD_CON = 0x80000c21`, y
  `0x080b6f8c` sólo produce ese valor por la rama `lcd_get_type() ∈ {0,1}`, que
  es `LCD_CMDSET_8BIT` de Rockbox (`lcd-6g.c:201-214`).
- **320×240**: los tres registros de driver de panel entre los que elige el
  firmware (`0x08911f90`, `0x08911fb8`, `0x08911fe0`) empiezan los tres por
  `0x00f00140` = `(240<<16)|320`; la ventana que fija es `0..0x13f` × `0..0xef`;
  y `+0x74` recibe `(320<<16)|240`.
- **RGB565**: el modo de trama es `LCD_MODE_P16`.

Validación con caso de control, porque un panel que no se prueba no es una
medida: `tools/ipod_classic/lcdtest.S` emite exactamente esa secuencia y empuja
76 800 palabras. `screendump` + `tools/ipod_classic/pixcount.py` devuelven
**76 800 píxeles encendidos y 1920 colores distintos**, que es al píxel lo que
el test escribe. Si la aritmética de ventana o el formato estuvieran mal, ese
número saldría mal de forma evidente.

```bash
arm-none-eabi-as -o /tmp/l.o tools/ipod_classic/lcdtest.S
arm-none-eabi-objcopy -O binary /tmp/l.o /tmp/l.bin
qemu-system-arm -M iPod-Classic -kernel /tmp/l.bin -display none -monitor stdio
(qemu) screendump /tmp/shot.ppm
python3 tools/ipod_classic/pixcount.py /tmp/shot.ppm
```

Cuando el retailOS dibuja (ver §4, ocurre en el camino de error) emite
`0x2A`, `0x2B`, `0x2C` — *column address set*, *page address set*, *memory
write* — que es el juego de órdenes de un controlador tipo ILI9341. Cuál está
puesto de verdad se elige en tiempo de ejecución con el *strap* de GPIO
`PDAT(6)` bits [5:4], que no modelamos.

---

## 4. El ciclo de caída sin disco, y por qué era invisible

Sin `-drive`, la máquina no se quedaba ociosa: **se caía y reiniciaba**, unas
diecinueve veces cada veinte segundos. Con `-d int`: 19 *Data Abort*, 19
llamadas de semihosting y 19 escrituras de `SWRCON`.

No se veía porque el diagnóstico salía por **stderr** y las trazas lo tiraban.
El firmware lo estaba diciendo en voz alta todo el tiempo:

```
CPU_exception_handler
qemu-system-arm: warning: S5L8702: guest asked for a software reset (SWRCON = 0xaa5), next image "--------"
```

> Lección, y es la número 1 de [09-historia-5g.md](09-historia-5g.md): si el
> firmware imprime algo, escúchalo. El semihosting ya estaba encendido por
> defecto en esta máquina y aun así estuvimos un rato mirando histogramas de
> `-d unimp` en vez de leer las dos líneas que explicaban todo.

### La causa

```
...with DFSR 0x5 DFAR 0xea00002a
```

`0xEA00002A` no es una dirección: es una instrucción de salto ARM. Y es
`0xEA000002 + 0x28`, donde `0xEA000002` es exactamente la palabra que `main`
escribe en `[0x22000000]` (paso 3 de su inicialización).

La instrucción que aborta es un par de saltos de C++:

```asm
0815b230:  ldr r0,[sp]        ; puntero al objeto
0815b234:  bic r0,r0,#1
0815b238:  ldr r1,[r0]        ; puntero a la vtable
0815b23c:  ldr r1,[r1,#0x28]  ; <-- aborta
0815b240:  blx r1
```

Es una **llamada a método virtual sobre un objeto nulo**. Y aquí está lo
interesante: como el propio firmware mapea VA 0 sobre IRAM0, la desreferencia
nula **no falla** — lee tranquilamente una palabra de la tabla de vectores — y
sólo revienta la segunda. Un puntero nulo en esta máquina no da un fallo
limpio: da un valor plausible y una caída doscientos bytes más tarde.

**Corrección de agosto de 2026, y es una corrección de fondo: el nulo NO viene
de la búsqueda de recursos.** Medido con `gdbpoke.py` (§10), parando en cada
instrucción de esa función:

| Parada | Qué se lee |
|---|---|
| `0x0815b1e0` | `r0 = 0x086aef48` — la búsqueda de `"FONT"` **encuentra** el recurso |
| `0x0815b1f4` | `r0 = 0x0874a444` — la de `"Str "` **también** |
| `0x0815b20c` | `r5 = 0x0be6770c` — no nulo, no salta a la salida |
| `0x0815b230` | `[sp] = {0, 0}` — **aquí** aparece el nulo |
| `0x0815b23c` | `r1 = 0xea000002`, la palabra de VA 0; la siguiente carga aborta |

Y el registro `FONT` que devuelve, leído del huésped, es
`0dad0001 0000003a 01d58101 0dad0002`: id del nombre, cuerpo 58, estilo.

O sea que la cadena de proveedores funciona y el recurso existe. Lo que falla es
la construcción del objeto de texto, y la razón es concreta:

```
0x0815b22c  bl 0x08239658   "hazme una cadena con esta fuente"
0x08239658  -> 0x08238fa0 -> 0x082393ec
0x082393ec  busca la fuente por nombre, cuerpo y estilo en DOS listas:
              [0x08912cd8 + 8]  fuentes ya instanciadas
              [0x08912cd8 + 4]  ficheros de fuente disponibles
            si ninguna casa, devuelve {0,0}
las dos listas las llena una sola función, 0x08239220, y hace esto:
  0x080518a4(1, &vol, &path)   ->  vol = 4, path = "Resources/Fonts"  (0x08380628)
  0x08239134(vol, path)        ->  recorre ESE DIRECTORIO y registra cada fichero
```

`0x08239134` es un iterador de directorio de verdad — salta las entradas que
empiezan por `.` (compara con 46) y llama a `0x08107184` por cada nombre.

**Conclusión, VERIFICADA de punta a punta: el retailOS no lleva ninguna fuente
dentro de la imagen. Todas viven en `\Resources\Fonts\` de la partición `rsrc`
del disco** (`Helvetica.ttf`, `HelveticaBold.ttf`, `CJK11.ttf`, `CJK16.ttf`,
cuatro `MonoHope-*.sbit`). Sin volumen montado la lista está vacía, no casa
ninguna fuente, y el llamador llama a un método virtual sobre el `{0,0}`.

Lo confirma un experimento no destructivo: con `gdbpoke.py --skip
0x0815b230=0x0815b270:0` se salta esa llamada sin tocar la imagen de Apple, y el
arranque **vuelve a caer en el mismo ciclo**, ahora con `DFAR 0xEA00003E` en vez
de `0xEA00002A` — otro método virtual, ranura `+0x3C`, sobre otro nulo del mismo
origen. No es un fallo que se pueda esquivar: sin fuentes, todo el grafo de
objetos de la interfaz sale nulo.

**Con un disco enganchado, el ciclo entero desaparece** — pero por la razón mala:
la interfaz ni siquiera llega a intentarlo (§9).

---

## 5. El puente ATA de `0x38700000`

No es un *task file*: los ocho registros de dispositivo están en `0x54..0x74`
pero no se leen directamente. Leer uno arranca un ciclo de bus cuyo resultado
aparece después en `ATA_PIO_RDATA`. Rockbox lo escribe entero
(`storage_ata-6g.c`) y el driver de Apple es la misma función compilada de otra
fuente (`0x08096824`).

### El DMA

Especificado dos veces, y las dos coinciden: Rockbox en
`storage_ata-6g.c:851-880`, y el driver de Apple trazado registro a registro.

```
<orden ATA 0xc8 a ATA_PIO_CSD>
ATA_TBUF_START = buffer        (ATA_SBUF_START al escribir)
ATA_TBUF_SIZE  = bytes
ATA_XFR_NUM    = bytes - 1
ATA_CFG |= flags ; ATA_CFG &= ~(BIT(7)|BIT(8))
ATA_IRQ = BITRANGE(0,4) ; ATA_IRQ_MASK = BIT(0)
ATA_COMMAND = BIT(0)           <- y sólo ahora se mueve
<espera la interrupción>
ATA_COMMAND = BIT(1)           <- fin
```

`ATA_CFG` bit 4 elige el sentido: puesto = búfer de origen = escritura a disco.

**El orden es lo que da forma al modelo, y es el contrario del intuitivo.** La
dirección del búfer se programa *después* de la orden ATA, así que `start_dma`
no puede saber dónde va el dato: sólo puede quedarse con el *callback* que le
da el núcleo IDE, y es la escritura del bit de arranque la que lo dispara.

Dos trampas que cuestan una tarde cada una:

- `s->bus.dma` hay que reclamarlo **después** de `ide_bus_init_output_irq`, no
  antes: lo último que hace esa función es poner `bus->dma = &ide_dma_nop`.
  Reclamarlo antes hace que toda orden de DMA se acepte y se ignore en
  silencio, que se parece muchísimo a un disco que no contesta.
- Comprobar el DMA por la ausencia de quejas no vale. Se comprueba leyendo la
  memoria del huésped: tras la primera transferencia, `0x0be5d3e0 + 0x1fe`
  contiene `55 aa`, la firma del MBR del sector 0 de la imagen, en el byte que
  le toca.

---

## 6. Dónde se para hoy

Con disco, el sistema llega a su **bucle idle** y se queda:

```
22003b1c:  mcr p15,0,r0,c7,c0,4    ; wait for interrupt (ARM926)
22003b30:  b   0x22003b0c
```

`PC = 0x22003b20`, modo SVC, `SP = 0x22010b70` (la pila SVC inicial). El kernel
RTXC está arrancado, el planificador corre, las interrupciones de timer entran,
y **todas las tareas de aplicación están bloqueadas**.

Lo que sí hace antes de dormirse:

- MIU, reloj, PMU por I²C, GPIO, click wheel, controlador de interrupciones
  externas, y el ATA.
- Dos lecturas por DMA, las dos en **LBA 0**: ocho sectores y luego sesenta y
  cuatro. Los bytes llegan (verificado, §5). Después, nada más.
- Sondea `WHEELINT` (`0x3C200014`) unas **120 veces por segundo** — medido en
  la auditoría con dos carreras de 20 s y 50 s: 3071 y 6770 lecturas, o sea
  123/s en régimen. (La primera versión de esta página decía «tres veces por
  segundo»; era falso por un factor de 40.) Sigue sin ser un giro bloqueante —
  un giro son millones — y sigue demostrando lo mismo: **hay tareas corriendo
  además de la idle**, que es la mejor prueba de que el planificador vive.

### Lo que NO se ejecuta, y esto es el dato importante

| Dirección | Qué es | ¿Se ejecuta? |
|---|---|---|
| `0x0805712c` | el parser de la partición de firmware: en `0x08057198` compara `]ih[` (`0x5B68695D`, verificado en `0x080572d4`) con el offset `0x100` de lo que leyó | **no, nunca** |
| `0x0815b1b8` | el código de UI que dibuja texto (el que caía sin disco) | **no** |
| `0x08239658` | la construcción del objeto de cadena | **no** |
| `0x08236130` | la búsqueda de recursos | sí, una vez |

> **Corrección (auditoría).** La primera versión de esta tabla medía el parser
> por `grep -c '^0x08057160'`, y **esa medida no valía**: `0x08057160` es
> `add r0,sp,#4` en mitad de una función, no una entrada, así que un log de
> `-d in_asm` habría dado cero aunque el código corriera en cada arranque (ver
> §8). La entrada real de la función es `0x0805712c` — `push {r1..fp,lr}` —, y
> medida por ahí la conclusión **se mantiene**: cero, con disco y sin él. Las
> otras tres direcciones sí son entradas de función (`push` como primera
> instrucción), así que ésas siempre fueron válidas.

O sea: **el bloqueo está aguas arriba tanto del parseo de la partición de
firmware como del dibujado**, y no es el almacenamiento, porque el
almacenamiento funciona.

### Hipótesis descartadas con medida

| Hipótesis | Descartada porque |
|---|---|
| "Le falta un MBR válido" | Con MBR y dos particiones (tipo 0 + FAT32) hace exactamente las mismas dos lecturas |
| "La partición de firmware tiene que estar en LBA 0" | Con la imagen `Firmware-24.9.1.2` cruda en el sector 0, y `]ih[` verificado en `0x100`, las mismas dos lecturas |
| "El disco es demasiado pequeño" | Con 64 MB, 289 MB, 512 MB y 80 GB: las mismas dos lecturas |
| "Está bloqueado en el DMA de ATA" | Lo estuvo, y ya no: las transferencias terminan y el huésped acusa la interrupción |
| "Está bloqueado en el LCD" | Lo estuvo, y ya no; ahora sólo toca el bloque dos veces y para |
| "El click wheel lo tiene parado" | Lo sondea ~123 veces/s desde una tarea periódica (medido: 3071 lecturas en 20 s, 6770 en 50 s); no es un giro bloqueante |

### Lo siguiente que hay que averiguar

Qué está esperando la tarea bloqueada. La vía más prometedora no es adivinar
periféricos, es preguntárselo al kernel: la imagen trae **RTXCbug** compilado,
el depurador interactivo de RTXC, con su REPL (`"RTXCbug> "`, `"Bad command"`,
`"G - Go to Multitasking Mode"`, `"** Task Register Snapshot **"`) y un
retarget completo de semihosting, incluido `SYS_READC`. Si se consigue entrar,
dice directamente qué tareas hay y en qué están bloqueadas.

Candidatos por descartar, en orden de coste:

1. El bloque `0x38900008`, que la máquina no mapea y que el retailOS lee y
   escribe cientos de veces en el camino de error. `INDETERMINADO`.
2. GPIO de verdad: hoy devuelve cero a todo. `PDAT(11)` bit 1 = 0 elige ATA
   paralelo, que es lo que queremos, pero `PDAT(6)` (tipo de panel) y
   `PDAT(10)` (auriculares) también leen cero y puede que no deban.
3. El EIC (`0x39A00000`), que se programa y nunca dispara.
4. DMAC0 (PL080), que se programa con descriptores en DRAM y nunca completa.

---

## 9. Con disco: dónde se para la interfaz, instrucción a instrucción

Esto es nuevo, y responde a «qué está esperando la tarea bloqueada» del §6, que
llevaba abierto desde el principio. Todo medido con `gdbpoke.py` y un disco de
64 MB a ceros; con `A.img` (MBR válido, partición de firmware real y FAT32 con
`iPod_Control`) sale exactamente igual.

La cadena, de fuera adentro. Cada eslabón es una parada real del depurador:

```
0x0811c008   crea la tarea RTXC "MP3ExampleTask"
0x0811bf68     cuerpo de la tarea de interfaz          <- llega
0x0811bfb8       blx [obj+0x204] -> 0x081a3134         <- entra y NO vuelve
0x081a3134         blx [obj+0x208] -> 0x081a3360       <- entra y NO vuelve
0x081a3360           bucle sobre 9 fábricas de subsistema; se para en la nº 8
0x0824caa8             new(0x124) + constructor 0x0824cc8c
0x0824cc8c               bl 0x08035bf8                 <- lector de SysInfo
0x08035bf8                 bl 0x080ca440
0x080ca440                   bl 0x0815f818
0x0815f818                     bl 0x0815f7c0
0x0815f7c0                       bl 0x0812289c
0x0812289c   81228b4: bl 0x0805c430      ; toma el mutex de [obj+0x2c]
             81228c4: ldrb r0,[r4,#0x28] ; <- LA BANDERA
             81228c8: cmp  r0,#0
             81228cc: beq  0x81228bc     ; 81228bc: bl 0x080c8968  = esperar
```

**`while (![obj+0x28]) wait();`** — la interfaz espera a que haya un volumen
montado. Con disco, `0x081228d0` (la salida del bucle) no se alcanza jamás; sin
disco sí. La CPU se va al bucle idle porque la tarea está bloqueada en un objeto
del kernel, no girando: por eso 27 000 IRQ y ninguna otra excepción.

Quién levanta la bandera: **sólo** `0x08122a04`, con dos llamadores,
`0x08059ae4` y `0x081229f0`, los dos dentro de `0x08059aa0` = «montar el
volumen». Y `0x08059aa0` tiene **un solo** llamador, `0x083287e0`, que es el
cuerpo de la tarea que consume la cola 4:

```
0x0832878c   for(;;) { msg = KS_receive(4); switch (msg[0x10]) {
                 case 0: 0x08059aa0(dev, 0);   // montar
                 case 1: 0x08126638(dev);      // sin medio
                 case 2: 0x081263dc(dev); ... } }
```

Sin disco, esa tarea recibe mensajes y monta (falla el sondeo, pero **levanta la
bandera igual** y la interfaz sigue). Con disco **no recibe ninguno**:
`0x083287b4` no se alcanza en 90 s.

### Y el mensaje sí se envía: el que no llega a escuchar es el receptor

Esto hay que decirlo con cuidado porque la primera lectura fue la equivocada. El
único emisor de mensajes de tipo 0 de la imagen es `0x0813b160` (mensaje
estático en `0x0813b21c`, `[msg+0x10] = 0`), al que se llega desde `0x0813b220`,
que es el cuerpo de la tarea **`"ATAWorkLoopTask"`** (cadena en `0x0813b2bc`,
creada en `0x0813b234` con prioridad 24). Parecía razonable pensar que con disco
no se ejecutaba. **Es falso, y medirlo cuesta un minuto:** con `A.img` y sin
disco las cuatro primeras paradas de `0x0813b160`, `0x0813b1ac` y `0x0813b1c0`
son **idénticas registro a registro**. El mensaje se envía en los dos casos.

Lo que cambia es el **receptor**. Antes de su primer `KS_receive(4)`, la tarea
`0x0832878c` hace dos llamadas:

```asm
832878c: push {r4,r5,r6,lr}
8328790: mov r0,#1
8328794: bl 0x081c207c        ; coger el dispositivo 1     <- vuelve
8328798: mov r5,r0
832879c: bl 0x08126638        ; <- CON DISCO NO VUELVE
83287a0: mov r1,#0 ; mov r0,#4 ; bl 0x080159a0   ; recibir  <- nunca se llega
```

Medido en las dos configuraciones: sin disco las paradas llegan hasta
`0x083287a0` y se repiten; con disco se paran en `0x0832879c` y ahí acaban.

Y de ahí hacia abajo, cada eslabón es otra parada real:

```
0x08126638   lock([dev+0x10]) ; blx [[dev+4]+0x18] -> 0x0823d420
0x0823d420     el sondeo de almacenamiento: reserva 4096 B, LBA 0
0x0823d52c       bl 0x0827aad0                       <- CON DISCO NO VUELVE
0x0827aad0         bl 0x0827a3f8 ; blx [r0+8]        <- vuelve (r0 = 0)
0x0827ab10         bl 0x0827aa1c -> 0x0827a980       ; leer y clasificar LBA 0
0x0827a9ac           blx r3 -> 0x080aea1c            <- AQUÍ SE PARA
                       r0 = 0x8be5d3e0 (alias sin caché de 0x0be5d3e0),
                       r2 = 1  -> "léeme UN bloque"
0x080aea1c             blx [[0x08966004+4]+0 +0x0c]  ; el read del almacenamiento
```

`0x0827a980` es corto y lo dice todo: llama al read, y si vuelve con cero mira
`buf+0x1FE` buscando `0xAA55` (→ `0x080aee04`, parser de MBR) y si no `buf+0`
buscando `0x4552` `'ER'` (→ `0x080a38cc`, parser de Apple Partition Map). **Nunca
llega a mirar.**

Y lo que hace que esto sea un hallazgo y no un callejón: **la lectura sí
ocurre**. `ATATRACE=1` enseña la orden `0xC8`, `ATA_TBUF_START = 0x0be5d3e0`,
`ATA_TBUF_SIZE = 0x1000`, la transferencia, el huésped leyendo `ATA_IRQSTAT`,
reconociéndola con un uno y parando el motor con `ATA_COMMAND = 2`. El disco
entrega, la interrupción llega y el huésped la acusa — y aun así la llamada de
lectura no vuelve. **El agujero está entre «el DMA terminó» y «despierta a quien
lo pidió»**, no en el ATA.

Es el mismo punto con disco a ceros, con `A.img` (MBR de 512 bytes, partición de
firmware real y FAT32 con `iPod_Control`) y con `E.img` (MBR en unidades de
4096): las tres se paran ahí, y las tres dan trazas de ATA idénticas de 682
líneas. El contenido del disco sigue sin importar, y ahora se sabe por qué:
nadie llega a mirarlo.

### Lo que se arregló por el camino: GPIO pin 86

Hay un segundo camino hacia la misma bandera y estaba roto por un bit.
`0x08328720` es un bucle de sondeo que decide entre «montar» y «sin medio»:

```asm
bl 0x08294d7c ; cmp r0,#0 ; bne sin_medio     (PMU 0x4b bit 2)   medido: 0
bl 0x08295508 ; cmp r0,#0 ; bne sin_medio     (PMU 0x12/0x4b)    medido: 0
bl 0x080a1474 ; cmp r0,#0 ; bne sin_medio     ([0x089120a1]!=100) medido: 0
bl 0x080aeca4 ; cmp r0,#0 ; movne r1,#0       gpio_get_pin(86)   medido: 0  <-
mov r0,#4 ; bl 0x080b2ce8                     ; anunciar {r1}
```

`0x080aeca4` es literalmente `gpio_get_pin(86)`, y `0x3CF00000` era
`create_unimplemented_device`, o sea cero: el firmware concluía «no hay medio»
aunque hubiera disco. Ahora hay un modelo de GPIO
(`hw/arm/ipod_classic/ipod_classic_gpio.c`) y el pin 86 sigue a si hay `-drive`;
medido después del cambio, `0x080aeca4` devuelve 1 y el mensaje pasa a ser de
tipo 0. **Necesario y no suficiente**: `0x080b2ce8` despacha por
`[[0x08911ad8]+0x10]`, y ese puntero sigue valiendo 0 a los 35 s, así que el
anuncio se tira. Lo que el pin 86 arregla es real y estaba mal; lo que queda es
otra cosa.

Qué es el pin 86 en la placa: `INDETERMINADO`. Qué hace en el firmware de Apple:
verificado arriba.

### El obstáculo concreto que queda

> **Con disco, la lectura de un bloque de LBA 0 (`0x080aea1c`, llamada desde
> `0x0827a9ac`) no vuelve nunca, aunque el DMA de ATA se completa y el huésped
> acusa la interrupción.** Todo lo demás está comprobado y funciona: el ATA
> entrega los bytes, los proveedores de recursos están montados, el contenedor
> empotrado está en RAM, el panel dibuja lo que se le mande y la ventana se fija
> bien.

Lo que se sabe del ATA en ese momento (`ATATRACE=1`, disco a ceros, `A.img` y
`E.img`: **las tres trazas son idénticas, 682 líneas**):

- La orden `0xEC` (IDENTIFY) se completa.
- Dos lecturas por DMA, las dos en LBA 0: `0x1000` bytes a `0x0be5d3e0` y
  `0x8000` bytes a `0x0be55320` — que son justo el búfer de 4096 de
  `0x0823d420` y su segunda pasada.
- Después el driver cierra limpiamente (`ATA_CFG = 0x640`, lee `+0x70`/`+0x7c`)
  y **no vuelve a hablar**. No está girando sobre ningún registro: el bloqueo
  está por encima del driver.
- Sin disco la traza acaba en `WR +0x70 = 0xec` sin respuesta. El IDENTIFY falla,
  `0x0823d420` sale por su rama temprana (`0x0823d44c`) sin llegar nunca a
  `0x0827aad0`, y por eso el camino sin disco no se bloquea: **se bloquea el que
  encuentra un disco, no el que no lo encuentra.**

Lo primero que hay que hacer mañana es entrar en `0x080aea1c` con `gdbpoke.py`
—`blx [[0x08966004+4]+0 +0x0c]`, dos o tres niveles más— y averiguar en qué
objeto del kernel se queda dormida la petición. La sospecha, `INFERIDO`: la
notificación de fin de transferencia no llega a la tarea que la espera. Dos
cosas que descartar primero, las dos baratas: que la línea de interrupción del
ATA (`S5L8702_ATA_IRQ`) esté en el VIC que el firmware ha desenmascarado, y que
`0x0823d420` no esté esperando además una segunda interrupción del bloque de
ATA que el modelo no genera (el modelo sólo levanta `ATA_IRQSTAT` bit 0, desde
INTRQ del núcleo IDE; qué hacen los demás bits es `INDETERMINADO`).

`INFERIDO`, y hay que comprobarlo antes de creérselo: si eso se destraba, se
monta un volumen; si el volumen es la partición `rsrc` del `.ipsw`, aparece
`\Resources\Fonts\Helvetica.ttf`; con eso `0x082393ec` deja de devolver `{0,0}`
y la composición de la interfaz puede terminar. Sólo entonces se sabrá de dónde
lee el motor de trama de `+0x70`, que es la última incógnita del camino a
píxeles.

---

## 7. Cómo reproducirlo

```bash
wsl -d Ubuntu-24.04 -e bash -c "cd /mnt/c/Desarrollo/qemu-ios/build && ninja qemu-system-arm"
cd build

# Sin disco: el ciclo de caída. Mirar STDERR, que es donde habla el firmware.
./qemu-system-arm -M iPod-Classic -kernel ../assets/classic6g/osos_decrypted.bin \
    -display none -monitor none -serial null

# Con disco: arranque limpio hasta el bucle idle.
dd if=/dev/zero of=/tmp/ipod.img bs=1M count=64
./qemu-system-arm -M iPod-Classic -kernel ../assets/classic6g/osos_decrypted.bin \
    -display none -monitor none -serial null \
    -drive if=ide,format=raw,file=/tmp/ipod.img

# Qué periféricos pide y cuáles no están.
... -d unimp,guest_errors -D /tmp/u.log

# Toda la conversación con el ATA, que es como se sacó el protocolo del §5.
ATATRACE=1 ... 2>/tmp/ata.txt

# Dónde está parado.
(sleep 25; printf 'info registers\n'; sleep 2; printf 'quit\n') | \
    ./qemu-system-arm ... -monitor stdio
```

Para saber si un trozo de código llegó a ejecutarse — la pregunta más útil de
toda la sesión — `-d in_asm` y buscar su dirección:

```bash
./qemu-system-arm ... -d in_asm -D /tmp/asm.log
grep -c '^0x0805712c' /tmp/asm.log     # 0 = ningún bloque empezó ahí
```

> **Y aquí está la trampa, que costó una medida falsa.** En esta compilación
> `-d in_asm` no desensambla (no hay capstone): imprime **una sola línea de
> dirección por bloque de traducción**, la del principio, seguida de
> `OBJD-T: <bytes>`. Comprobado: `0x22003b1c` — el `wfi` del bucle idle, que se
> ejecuta miles de veces — sale **cero** veces, porque su bloque empieza en
> `0x22003b0c`.
>
> Luego `grep -c '^0xDIRECCION'` sólo responde «¿corrió esto?» si la dirección
> es **entrada de bloque**: destino de un `bl`/`b`, o el `push` con el que
> empieza una función. Para cualquier otra da cero siempre y no significa nada.
> Antes de usarlo, desensambla la dirección y comprueba que hay un prólogo.

---

## 8. Lo que se aprendió del método

- **`-d cpu` no sirve aquí.** Genera 2 GB y 45 millones de líneas en 25 s y ni
  siquiera llega al primer fallo. Para localizar una excepción, `-d int,in_asm`
  interleaved: el bloque de traducción que se imprime justo antes de
  `Taking exception` es el que la provoca.
- **`grep -c '^0xDIRECCION'` sobre un log de `in_asm`** responde "¿esto llegó a
  correr?" en un segundo, y esa pregunta reordenó toda la investigación: sin
  ella habríamos seguido arreglando el parser de la partición de firmware, que
  nunca se ejecuta. **Pero sólo vale para entradas de bloque** — ver el aviso
  del §7. La primera versión de esta página lo aplicó a una dirección de en
  medio de una función y sacó un cero que no significaba nada; dio la casualidad
  de que la conclusión era correcta por otro camino. Es exactamente el error de
  método que avisa [09-historia-5g.md](09-historia-5g.md): comprobar la
  herramienta antes de creerse la medida.
- **Un puntero nulo no siempre falla.** En esta máquina VA 0 está mapeado por
  el propio firmware, así que `ldr r1,[r0]` con `r0 = 0` devuelve una palabra
  de la tabla de vectores y la caída ocurre más tarde y en otro sitio. `DFAR`
  con pinta de instrucción ARM (`0xEAxxxxxx`) es la pista.
- **Contar es necesario, pero no basta.** "Una palabra de píxel en 45 s" es un
  hecho; "luego el framebuffer va por DMA" era una inferencia con al menos otra
  explicación igual de buena (que el firmware se cae antes de empujar píxeles),
  y se coló como si fuese medida. Cuando una cuenta sale casi cero, la primera
  hipótesis a descartar es "no llegó a intentarlo". (Resuelto: eran cero
  píxeles y ocho parámetros de orden; ver §3.)

---

## 10. `gdbpoke.py`, y por qué cambia el ritmo de trabajo

`-d in_asm` sólo responde «¿se tradujo un bloque que empieza aquí?». La pregunta
que hacía falta era «¿qué hay en `r0` cuando el huésped llega a esta dirección?»,
y para eso hace falta un depurador. Esta WSL no tiene `gdb-multiarch`, así que
hay uno nuevo de 200 líneas que habla el protocolo remoto de GDB directamente
contra el `-gdb tcp::PORT` de QEMU:

```bash
sh tools/ipod_classic/gdbrun.sh 1300 $HOME/6g/z64.img 90 \
   --break 0x0815b1e0 --break 0x0815b230 --hits 6 --mem 0x0bede7a4:16
```

`gdbrun.sh` arranca QEMU con `-S -gdb`, espera y lanza `gdbpoke.py`.
`QEMU_EXTRA` añade argumentos (`-d int -D ...`). Todo el §9 y la corrección del
§4 salen de esta herramienta; sin ella habrían sido días de `-d in_asm`.

Además de `--break`/`--mem`/`--step` tiene `--skip FROM=TO[:R0]`, que reescribe
el PC en una parada: sirve para **saltarse una llamada sin tocar la imagen del
huésped**, que es como se demostró que esquivar el fallo de `0x0815b230` no
lleva a ninguna parte (§4).

> **Trampa que costó una hora y que hay que conocer.** QEMU **no** tiene lógica
> de «ignora el breakpoint del que acabo de salir»:
> `accel/tcg/cpu-exec.c:346` dispara siempre que `pc == bp->pc`. Un `c` pelado
> desde una parada vuelve a parar en el mismo sitio **para siempre**, y la
> primera versión de la herramienta sacó ocho paradas idénticas de una función
> con 374 llamadores sin que saltara ninguna alarma. La salida está tres líneas
> más arriba en el mismo fichero: *«Singlestep overrides breakpoints»*. `cont()`
> hace `s` y luego `c`.
>
> Y el caso de control que lo destapó: poner el breakpoint en una función con
> muchos llamadores y mirar si `lr` cambia entre paradas. Si no cambia, la
> herramienta está rota, no el firmware.

Segunda trampa, de direcciones: **los breakpoints son sobre direcciones
virtuales**. El firmware mapea VA 0 sobre IRAM0, así que la tabla de vectores se
ejecuta en `0x00000000`, no en `0x22000000`, y el manejador de Data Abort está
en **`0x00003400`** (`[0x22000010] = 0xEA000CFA`). Poner el breakpoint en
`0x22003400` no dispara nunca.

## 11. Ficheros de esta tanda

| Ruta | Qué es |
|---|---|
| `hw/arm/ipod_classic/ipod_classic_lcd.c` | ahora con panel: memoria 320×240 RGB565, ventana, `0x2A/0x2B/0x2C`, `qemu_console`, y los registros `+0x70..+0x8C` del motor de trama |
| `hw/arm/ipod_classic/ipod_classic_gpio.c` | GPIO. Un solo pin modelado, el 86, y la razón está en la cabecera |
| `tools/ipod_classic/gdbpoke.py` | cliente del protocolo remoto de GDB: breakpoints, registros, memoria, saltos |
| `tools/ipod_classic/gdbrun.sh` | arranca QEMU con `-S -gdb` y lo conduce |
| `tools/ipod_classic/lcdtest.S` | caso de control del panel: dibuja una rampa de 320×240 |
| `tools/ipod_classic/pixcount.py` | cuenta píxeles no negros de un `screendump` |
