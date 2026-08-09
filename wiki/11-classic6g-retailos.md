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
| 4. Dibuja algo en pantalla | **no** — ver §6 |

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

### Por dónde pasan los píxeles: `INDETERMINADO`

Lo medido es esto y sólo esto: en un arranque entero llega **una** palabra de
píxel a `LCD_WDATA` por escritura de CPU. Lo que la primera versión de esta
página concluía de ahí — «el framebuffer va por DMA» — **no se sigue**, y la
auditoría lo rebaja a `INDETERMINADO`.

La explicación alternativa es al menos igual de buena: el retailOS emite
`0x2A`/`0x2B`/`0x2C` (fijar ventana y «escribir en memoria») y **se cae en la
instrucción siguiente**, en el código de dibujo de `0x0815b1b8` (§4). Es decir,
puede que no haya píxeles por MMIO porque nunca llega a empujarlos, no porque
vayan por otra ruta. Para decidirlo hay que llegar antes a un dibujado que no
aborte.

(El modelo sólo anuncia la **primera** escritura de píxel, no las cuenta; para
repetir la medida hay que instrumentar `ipod_classic_lcd.c` a mano.)

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

El nulo viene de `0x08236130`, que recorre una lista enlazada de proveedores de
recursos llamando a un método virtual de cada uno y devuelve cero si ninguno
tiene la etiqueta pedida. Las etiquetas son 4CC clásicas: `"FONT"`
(`0x464F4E54`) y `"Str "` (`0x53747220`), y justo encima de la función que cae
hay un `"COLR"`. `0x08239658` devuelve explícitamente `{0,0}` cuando su
manejador de entrada es nulo, y su llamador no lo comprueba.

**Con un disco enganchado, el ciclo entero desaparece.**

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
  hipótesis a descartar es "no llegó a intentarlo".
