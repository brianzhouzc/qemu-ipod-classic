# Historia técnica del port del iPod Video 5G

Cronología de cada obstáculo que hubo que quitar para que un iPod Video 5G
emulado arrancara sus dos firmwares reales, **incluyendo las hipótesis que
resultaron falsas**. Las equivocaciones ocupan aquí tanto espacio como los
aciertos, y a propósito: son lo que evita que alguien vuelva a recorrer el
camino malo.

Los otros documentos dicen **cómo es** el hardware
([07](07-pp502x-hardware.md)) y **en qué estado está** el trabajo
([04](04-roadmap.md)). Este dice **cómo se averiguó**, y qué costó.

## Cómo leer esto

Cada afirmación lleva el hash del commit donde se hizo y, cuando el resultado
quedó fijado en el árbol, fichero y línea. Hay dos niveles de confianza y los
distingo siempre:

- **Verificado en el árbol**: leído en el código actual de
  `hw/arm/ipod_video/` e `include/hw/arm/ipod_video/`, o ejecutado.
- **Sólo en el mensaje de commit**: cuentas de bloques traducidos, píxeles
  encendidos y direcciones del desensamblado del `osos`. Son internamente
  consistentes y varias quedaron replicadas en comentarios del código, pero
  **no se han reproducido una por una**. Van marcadas *(sólo commit)*.

`assets/` está fuera de git, así que las capturas citadas no viajan con el
repositorio; se regeneran con `navtest.sh` y `appletest.sh`.

## Las cuatro sesiones

51 commits sobre el merge upstream `328815631`, del 6 al 9 de agosto de 2026.

| Fecha | Commits | Qué se consiguió |
|---|---|---|
| 06-ago 14:04–14:30 | `994f52fde`, `2ddc18b2a` | Máquina `iPod-Classic` (S5L8702): timers e interrupción verificados |
| 06-ago 15:57–21:44 | `d9cf852ab` … `a38f51d07` | Volcado del 5G, máquina `iPod-Video`, el bootloader de Rockbox dibujando, y la primera cadena de pantallas negras |
| 08-ago 19:40–22:23 | `b3355f116` … `9baca880e` | Rockbox hasta el menú y navegable; primer diagnóstico del retailOS, equivocado |
| 08-ago 23:04 – 09-ago 07:45 | `9d823ec76` … `c68ce94d9` | El retailOS: de "cree que no hay RAM" a interfaz dibujada y navegable |

---

## Fase 0 — El Classic, y por qué el proyecto se mudó

El proyecto empieza en el S5L8702 (`994f52fde`): mapa de memoria derivado de
`firmware/export/s5l87xx.h` de Rockbox, ARM926EJ-S, VIC0/VIC1, UART0, y 32
periféricos como `create_unimplemented_device()` para que `-d unimp` diga a
cuál va el guest. Timers y una interrupción completa se verifican con un test
bare-metal (`2ddc18b2a`).

El muro: **el firmware del Classic está cifrado con una clave que vive en el
silicio**. `d9cf852ab` mide la diferencia en vez de discutirla — entropía de
5,2–6,3 bits/byte en el 5G contra 7,99 en el Classic, y 21.016 cadenas ASCII
legibles en el 5G, incluyendo `portalplayer` y los nombres de imagen del
cargador (`booting!`, `diskmode`, `diskscan`, `softupdt`, `retailOS`). Eso es
lo que mueve el objetivo al Video.

> **Lección**: la entropía por byte de un volcado es la prueba de cinco
> segundos de si vas a poder trabajar con ese firmware. Antes de planificar
> nada, mídela.

**Corrección posterior sobre el Classic** (`3f5cd7280`, `e17924eab`): la wiki
decía que había que escribir de cero ATA, click wheel y LCD del S5L8702. Es
falso: `freemyipod/qemu`, rama `s5l8702-dev`, ya tiene un modelo completo,
forkeado de la misma base `devos50/qemu-ios`. `e17924eab` corrige además el
mapa de `wiki/02`: se había derivado preprocesando `s5l87xx.h` y colaron bases
que sólo existen en S5L8700/8701 (SPI0, ADC, RTC, FMC, USB host/function,
`INT_BASE`, `MS_BASE`), y llamaba click wheel a "Mikey", que es en realidad el
micrófono/mando del jack, en I2C0 a `0x72`.

---

## Fase 1 — Sacar el firmware: dos bugs de parsing con la misma causa

`d9cf852ab` añade `tools/ipod_video/dump-firmware.ps1` y arregla dos cosas en
`fwtool.py`:

- El offset del directorio en la cabecera `]ih[` no es relativo al buffer, y
  **de qué es relativo depende de dónde venga el contenedor**: `0x4000`
  significa `0x5000` en un payload de `.ipsw`, pero `0x4200` en una partición
  leída del disco.
- El directorio no lleva longitud, así que el parser se salía por el final y
  reportaba 81 entradas antes de reventar imprimiendo un código de cuatro
  caracteres que no era texto.

`a513a1a50` encuentra la causa común: **todos los offsets del directorio están
sesgados 0x200 bytes**, porque cuentan desde la base de la imagen (el segundo
sector de la partición), no desde el principio del buffer.

El síntoma que lo delató es el detalle que merece copiarse: `bootloader/ipod.c`
de Rockbox dice que la cadena `"portalplayer"` está en `DRAM_START+0x20`, y
aparecía en `+0x220`. Con el sesgo corregido, el `osos` empieza con una tabla
de vectores ARM en condiciones, `rsrc` con un boot sector FAT, y la mitad
Rockbox con `msr cpsr_c, #0xd3`.

> **Lección**: cuando tienes una constante documentada por un tercero
> (`"portalplayer"` en `+0x20`), úsala como oráculo del parser. El sesgo de
> 0x200 explicaba a la vez el offset "raro" del directorio y los ceros donde
> debería haber vectores: **un solo error, dos síntomas que parecían
> independientes**.

### Conflicto de fuentes, resuelto a favor del silicio

`d9cf852ab`. El aparato reporta modelo `MA146E`, que las bases de datos dan
como 5G de 30 GB con 32 MB de RAM y un PP5021C. Rockbox lee **PP5022C y 64 MB**
del silicio, y el `osos` lleva `vers 0x0000b012` (`0xb0` = "Video/5G" para
`ipodpatcher`). La unidad lleva un adaptador iFlash, así que lo más probable es
que sean piezas mezcladas. Manda lo que dice el chip.

Ese conflicto sigue abierto en un punto y conviene saberlo: ver
"[La talla de la DRAM](#la-talla-de-la-dram-cuestión-abierta)" al final.

---

## Fase 2 — La máquina arranca

`ddf21e4bc`. Decisión de arquitectura que ahorra un volcado de ROM: el cargador
de la ROM de Apple sólo copia el `osos` a `DRAM_START` y salta a
`DRAM_START + entryOffset`, así que **la máquina hace ese trabajo
directamente** y expone una propiedad `entry`. `0x735a00` es el bootloader de
Rockbox, `0` el firmware de Apple; los dos viven en la misma imagen.

Cuatro registros tuvieron que ser reales, cada uno porque el firmware se paraba
visiblemente en él:

| Registro | Por qué |
|---|---|
| `PROCESSOR_ID` `0x60000000` | Los dos núcleos ejecutan la misma imagen y se distinguen leyendo este byte (`0x55`/`0xaa`). Un stub a cero no es ninguno de los dos |
| `COP_CTL` `0x60007004` | `crt0-pp-bl.S` gira hasta que el bit 31 diga que el otro núcleo duerme |
| `USEC_TIMER` `0x60005010` | Contador libre de microsegundos: todos los busy-wait de Rockbox están escritos contra él; a cero, el primer delay es un bucle infinito |
| Ventana de flash NOR en 0, revisión de placa en `+0x405c` | `hwcompat.h` la lee de ahí durante el boot y de `0x2000405c` después, con el comentario "ROM is remapped" — así se supo que **la dirección 0 es flash, no RAM** |

El valor de revisión, `0x000b0005`, sigue en `ipod_video.h:213,220`.

`9d3b0ec76` modela el BCM2722 lo justo para que el bootloader dibuje. Detalle
que no es obvio y condiciona todo lo demás: **el panel no cuelga del SoC**,
cuelga de un chip Broadcom al que el PP502x llega por ocho registros espaciados
64 KB a partir de `0x30000000`.

---

## Fase 3 — La pantalla negra: una bisección correcta con tres conclusiones falsas

La parte más instructiva del historial, porque casi todo lo que se pensó fue
erróneo y quedó escrito.

**Síntoma**: al conectar el disco (`9d6b705c7`), la pantalla se quedaba negra.
Sin disco, el bootloader dibujaba.

### Hipótesis 1 — el alias de DRAM en baja memoria

`33e30cdb1` había aliaseado DRAM en direcciones bajas porque el firmware leía
`0x100000` decenas de miles de veces. `6ea6f90ed` sospecha que ese alias
corrompe la imagen en ejecución (`0x100000` → `0x10100000` cae dentro del
`osos`). Se quita: **la pantalla sigue negra**. Descartada — aunque el alias
era incorrecto igual y se fue.

### Hipótesis 2 — la entrega de interrupciones

TIMER1 ya disparaba y no había tabla de vectores en 0. Se desconecta la línea:
**sigue negra**. Descartada.

### Hipótesis 3 — `CACHE_CTL_VECT_REMAP`

`70d0bae2c` traza y descubre que la CPU **no está en un bucle**: va por
`0x000fe800`, `0x000fec00`, `0x000ff000`… en pasos de `0x400` hasta salirse del
mapa en `0x100000`. Es un **NOP sled** — una palabra de ceros decodifica como
`andeq r0, r0, r0`. El "Invalid read at 0x100000" que se veía era **el final de
ese paseo, no un spin**.

La deducción: se toma una excepción, no hay vectores en 0, y `pp5020.h` dice
que los vectores se remapean a IRAM por el controlador de caché en
`0x6000c000`. `2893ab659` lo implementa, y el título del commit lo dice todo:
*"implement the vector remap, which was not the answer"*. La ejecución resulta
**byte por byte idéntica**, 3157 bloques traducidos, mismo final *(sólo
commit)*. El firmware nunca pone ese bit en esa ruta. Se queda en el árbol
porque es correcto (`ipod_video.c:294-348`), pero no era la causa.

### La causa real

`90de12e29`: trazar **dónde entra** en memoria vacía, en vez de dónde sale.

```
0x400000b4  0x400000b8    bootloader de Rockbox, desde IRAM
0x10000000                salta al firmware de Apple
0x100001f0 .. 0x10000234  su reset path
0x0000023c                y a baja memoria
```

Dos cosas a la vez: **(a)** el bootloader estaba arrancando el firmware **de
Apple**, no Rockbox — el caso 1b de `bootloader/ipod.c`, que busca
`"portalplayer"` en `DRAM_START+0x20`; y **(b)** el firmware de Apple está
enlazado para la dirección 0 y necesita la unidad de remap de `0xf000f000`, que
seguía siendo un stub.

Y aquí la frase que hay que leer si retomas esto, del propio commit:

> *An earlier attempt at this aliased DRAM at 0x100000 and did not help, which
> is why the wrong conclusion was drawn from it — the alias belonged at 0.*

Un experimento mal montado descartó la hipótesis correcta durante cuatro
commits.

> **Lecciones**:
> 1. Un paseo ascendente en pasos regulares por memoria vacía es un **NOP
>    sled**, no un bucle. El "invalid access" que ves es el final; trazar hacia
>    atrás desde ahí no sirve, hay que encontrar **dónde empezó**.
> 2. Un experimento negativo sólo descarta la hipótesis **si el experimento
>    estaba bien construido**. "Aliaseé DRAM en baja memoria y no ayudó" no es
>    lo mismo que "aliaseé DRAM en 0".
> 3. Herramienta genérica en vez de parche puntual: `6702ab527` pone un
>    `unimplemented_device` de 64 KB **debajo** de las dos ventanas de registros
>    (`0x60000000` y `0x70000000`), con lo modelado por encima. El motivo, del
>    commit: *un registro desconocido que lee cero es sobrevivible; uno no
>    mapeado es un data abort*, y aquí el handler al que salta también falla y
>    no vuelve nunca. Modelar registros de uno en uno según van fallando es la
>    forma equivocada del arreglo.

---

## Fase 4 — Rockbox hasta el menú

`60541d2d8` agrupa tres bloqueos **del mismo tipo**: el hardware pone un bit,
nosotros devolvíamos cero, el firmware espera en bucle.

- **Los arrays de caché de `0xf0000000` son memoria direccionable, no
  registros.** `cache_invalidate_special()` recorre a mano el array de estado
  de línea. Basta RAM plana.
- **`+0x28` del bloque de control de dispositivo**: el bit 7 dice que el bloque
  USB salió de reset. No está en `pp5020.h`; sale de `usb-fw-pp502x.c`.
- **`GPO32` bit 14 alimenta el Broadcom.** El loader de la flash ROM de Apple
  lo enciende antes de ceder el control; `lcd-video.c` lo relee para decidir si
  tiene que subir el firmware del BCM desde la sección `vmcs` de la ROM. A
  cero, Rockbox se rendía, dejaba `display_on` en false, y `lcd_update_rect()`
  **descartaba cada dibujo sin decir nada**: todo se ejecutaba, nada aparecía.
  Sigue puesto a mano en `ipod_video.c`.

> **Lección**: cuando un driver tiene una ruta *"el hardware ya está
> inicializado, no hago nada"*, saltarse el cargador de ROM significa heredar
> la obligación de reproducir **todo** lo que ese cargador dejó hecho. `GPO32`
> bit 14 fue la primera de esas herencias; el bloque SysInfo (fase 5) fue la
> segunda.

### El descubrimiento de método: una captura es un instante, no un estado

`ad065fbeb`. *"No partition found"* no era el final de la historia, **era el
segundo fotograma**. Sacando capturas a intervalos se vio que a ~8 s el logo de
Rockbox 4.0 está en pantalla a todo color. El error venía del
`disk_mount_all()` de la **aplicación**, no del bootloader.

Con `-trace "ide_*"` las dos fases se separan limpias: el bootloader lee con
READ MULTIPLE (`0xc4`), la app emite READ DMA (`0xc8`) y la línea siguiente es
`ide_reset` — `ipodvideo` define `HAVE_ATA_DMA` y el mmio-ide genérico de QEMU
no tiene motor DMA.

`e2dba1271` lo resuelve y deja dos trampas escritas, las dos verificadas hoy en
el árbol:

- **`ide_bus_init_output_irq()` termina con `bus->dma = &ide_dma_nop`**, así
  que reclamar el bus **antes** de llamarla se deshace en silencio
  (`ipod_video_ide.c:240-246`).
- **`IDE0_CFG` INTRQ es un latch, no un espejo de la línea de interrupción**,
  porque durante DMA el firmware nunca lee el estado ATA: ese bit es la única
  señal de fin que recibe (`ipod_video_ide.c:63,92,121-125`).

### El click wheel: los datos no bastan

`a38f51d07` corrige la etiqueta del bloque `0x7000c000` (de I2C a click wheel),
`b3355f116` lo modela con teclado, y `e2dba1271` descubre el matiz:
`ipod_4g_button_read()` **sólo se llama desde `ipod_4g_button_int()`**, y ni
siquiera mira los datos si el bit 26 de `0x7000c104` está a cero. Hay que
**latchear el bit y levantar la interrupción**.

El paquete es `0x8000001a` con los botones como bits sueltos encima, 96 clicks
por vuelta, y Rockbox pide cuatro clicks de recorrido acumulado antes de mover
la selección, así que cada tecla avanza dos y una pulsación+suelta mueve
exactamente un elemento (`ipod_video.c:482-626`).

> **Lección**: leer cero de un registro de datos no significa "nada pulsado",
> significa "**ningún paquete**". Es una distinción que el firmware sí hace.

---

## Fase 5 — El retailOS: cinco muros y una cadena de hipótesis retiradas

### 5.1 "Necesita el segundo núcleo" — planteada y retirada

`d4259dea7` traza hacia atrás desde el SVC y concluye: el assert compara el
final de la imagen contra la RAM disponible, y la RAM disponible sale cero. El
campo cero se rellena desde una estructura que sólo escribe código guardado por
`cmp r0, #0xaa` (*¿soy el COP?*). Conclusión: hace falta modelar el COP.

`9d823ec76` modela el COP — y **no arregla nada**. El commit no lo adorna:
*"The retailOS still does not boot, and I am not going to dress that up."* El
COP se queda porque es hardware real y el modelo es correcto (lee `0xaa`, se
duerme como primer acto, exactamente como `crt0-pp.S`), pero no era la causa.

`288737573` retira formalmente **tres** hipótesis a la vez, tras dos
comprobaciones baratas:

1. *"`0x1084` es un puntero al bloque de parámetros de la ROM"* → **falso**:
   `0x1084` es "dame la dirección de la global N", devuelve
   `IRAM_END + N - 0x100`. Tiene 13 llamadores y varios escriben.
2. *"el COP rellena la global"* → **falso**: la ruta de la CPU
   (`0x25c → 0x15f8 → 0x1348 → 0x1244 → 0x15fc → 0x27eaec → 0x13bc`) no escribe
   `COP_CTL` en ningún punto. Y el commit lo confiesa: *"that was my inference
   from 0x95024 being COP-gated, not something I had verified."*
3. *"el flash en blanco es el bloqueo"* → ya retirada en `d4259dea7`, porque el
   firmware muere **antes** de mirar el flash.

> **Lección**: una inferencia por proximidad ("este código está guardado por el
> COP, luego lo escribe el COP") **no es una verificación**. La comprobación
> barata — *¿escribe alguien `COP_CTL` en la ruta?* — costaba minutos y ahorraba
> dos commits.

### 5.2 La cadena del chip: diagnóstico correcto, mecanismo equivocado

`0763f94eb` encuentra un muro real: `PP_VER1`/`PP_VER2` deletreaban `PP5022C`,
tomado de cómo **Rockbox** renderiza el aparato en su pantalla de debug. El
firmware de Apple compara contra los literales `"PP5020A "` y `"PP5022A "`;
`PP5022C` no coincide con ninguno, así que caía en la base "no reconocida" y
**todas** sus globales leían cero, incluido el tamaño de RAM.

**Pero el mecanismo estaba mal atribuido**, y `57ce76fab` lo corrige. El
comparador de cadenas de `0x27ea8c` existe, y su llamador `0xf0c` sí
convertiría el resultado en una base — pero **`0xf0c` es código muerto en este
build**: carga el literal de `0xf34` (`0x4001c8fc`), lo encuentra distinto de
cero y sale por el `popne` de `0xf1c` sin llegar a comparar. Quien decide de
verdad es `0x1084` vía `0x598`, que lee **un solo carácter** (bits 23:16 de
`PP_VER1`, `cmp r0, #0x32`). Todo el razonamiento está conservado en
`ipod_video.c:716-745`.

Valores actuales, verificados en el árbol (`ipod_video.c:746-747`):

```c
#define PP5022C_VER1        0x32324120   /* '2' '2' 'A' ' ' */
#define PP5022C_VER2        0x50503530   /* 'P' 'P' '5' '0'  */
```

es decir **`"PP5022A "`, no `"PP5022C"`**. Es contraintuitivo y por eso se
documenta aquí: el aparato real *es* un PP5022C, Rockbox lo llama así, y aun
así los registros tienen que deletrear la forma `A` porque es lo que el
firmware de Apple sabe leer.

> **Lección**: que un arreglo funcione **no valida la explicación** de por qué
> funciona. Aquí la explicación correcta y la incorrecta daban exactamente el
> mismo parche, así que el parche no distinguía entre ellas.

### 5.3 La premisa que sostuvo tres commits y era falsa

`0e7407722`, `4cbe50bcf` y `b9786a0bb` investigan bajo la premisa *"el firmware
muere en su primera llamada al sistema, porque espera sus handlers de excepción
ya instalados en IRAM"*.

`d06ce3123` la retira entera. **`0xe9b98` no es una syscall: es el impresor de
panics de la biblioteca C de ARM.** La delación está en la instrucción de
encima:

```
e9b94:  mov r0, #4          @ SYS_WRITE0
e9b98:  svc 0x123456
```

y en la tabla de cadenas justo arriba: `"Type: "`, `"undefined instruction"`,
`"data abort"`, `"_sys_exit"`, `"CPU "`, `"Task"`.

Con el semihosting contestado, **el firmware dice él mismo de qué se muere**:
cuatro bancos SDRAM a `FFFFFFFF` (vacíos), y una cadena de retorno que aterriza
exactamente en la rama que se creía que no volvía nunca. La IRAM vacía era
**consecuencia, no causa** — los handlers los instala el propio `osos` en
`0x2a0`, unos 150 bloques **después** del punto de muerte.

El semihosting queda **activado por defecto** en la máquina, con el
razonamiento escrito en `ipod_video.c:901-919`. Es el cambio de mayor
rendimiento de todo el proyecto: convierte cada futuro fallo de pantalla negra
en un mensaje con volcado de registros.

> **Lección, la más reutilizable del repositorio**: antes de trazar un firmware
> desconocido, **comprueba si ya te está hablando**. Un `svc` con un inmediato
> raro (`0x123456` es el mágico de ARM) y `r0 = 4` es `SYS_WRITE0`, no una
> syscall del SO. Activar semihosting es una línea.

De paso, `b9786a0bb` corrige un error de método de `4cbe50bcf`: la afirmación
de "139 literales de `0x40000000` en la imagen" era **falsa**, porque
`0x40000000` es también la palabra alta del double `2.0`, y una búsqueda de
bytes crudos encuentra tablas de coma flotante y las llama punteros.
`tools/ipod_video/findlit.py` decodifica cada `ldr rD,[pc,#imm]` y reporta sólo
literales que el código carga de verdad: **de 139 a 1**.

> **Lección**: buscar bytes en una imagen da falsos positivos a manos llenas.
> Si buscas un puntero, **decodifica las instrucciones que lo cargarían**.

### 5.4 El bit 26: un muro que nadie había propuesto

`2b1ba27e3`. La tabla de scatter-load del firmware (`0x84040..0x840a8`) recorre
la imagen de forma contigua, y en mitad del barrido las direcciones saltan
**exactamente `0x04000000`**:

```
src 0x006c9a64 + len 0x0004c734 = 0x00716198
next src                        = 0x0471619c
```

Una discontinuidad de un bit en una serie de offsets consecutivos no es la
tabla cambiando de dirección: es **la misma dirección con una bandera**. Y el
contenido lo cierra — el offset de fichero `0x7161a4` es `e1b0f00e`
(`movs pc, lr`), que tras la copia cae en `0x40000008`, justo donde apunta el
literal del vector SWI de `0xec`. Los handlers encajan al byte en cuanto se
ignora el bit 26.

Los alias están en `ipod_video.c:1000-1032`, con la constante en
`ipod_video.h:76` (`DRAM_ALIAS_BIT`). **Qué significa el bit para el hardware
real sigue siendo INFERIDO**, marcado así en el propio commit y en la cabecera.

Del commit, la frase que resume el riesgo de fiarse de un solo firmware:

> *Rockbox never forms a bit-26 address, which is why a machine that runs it to
> its menu could still not get the retailOS through a memcpy.*

> **Lección**: dos firmwares del mismo aparato ejercitan conjuntos **disjuntos**
> del hardware. Que uno arranque entero no dice casi nada sobre el otro.

### 5.5 El bloque SysInfo — la ROM revisitada, y el matiz que faltaba

`3b2637dc8`. `ipodloader2/ipodhw.c` de iPodLinux nombra
`SYSINFO_TAG_PP5022 0x4001ff18` y `SYSINFO_PTR_PP5022 0x4001ff1c`, y anota que
"los últimos 256 bytes los usa la Flash ROM para cosas especiales". El firmware
de Apple coincide al byte: `0x13bc` pide la global 24, la compara contra
`0x53797349` (`"IsyS"`) y toma un puntero de `+4`. **Nada en la imagen escribe
ninguna de las dos palabras** — el mágico lo carga exactamente **una**
instrucción en siete megabytes, y sólo para compararlo.

Ese es el criterio de decisión que merece la pena copiar:

> **Un valor que el firmware compara pero nunca produce tiene que venir de
> fuera de él.**

Dos fallos, y el segundo sólo se vio al arreglar el primero:

- **El tamaño de RAM.** Con puntero nulo, la lectura de `SysInfo+0xe0` va a
  `[0x000000e0]`, que tras el remap es una palabra de la propia tabla de
  vectores (`0x00000100`); 256 bytes redondean a cero, los cuatro bancos salen
  vacíos, y salta el assert. El detalle amargo: **el firmware ya sabía la
  respuesta** — `0x1f4` hace `mov fp, #0x700`, cuyo nibble 2 indexa la misma
  tabla a `0x02000000`. `0x13bc` lo tira a la basura a favor de lo que lee por
  el puntero nulo.
- **El id de hardware.** *"This is the one the earlier analysis had
  backwards."* Dejar el mágico a cero **parece** inofensivo, porque `0x13e8`
  desreferencia el puntero antes de que `0x1400` compruebe el mágico. Pero
  `0x140c` sólo publica el puntero en `0x4000608c` **si el mágico coincidió**, y
  sin eso `0x281224` no hace nada, `0x265164` lee `+0x84` de un objeto bss
  vacío, la tabla de salto de 13 entradas de `0x2653c8` responde "no such
  machine", y `0x2668e4` desreferencia un driver nulo.

**Se escriben cuatro campos y ni uno más** (`ipod_video.h:200-202`, escritos en
`ipod_video.c:1216-1229`), con el razonamiento explícito de por qué no se
rellena el resto: `sysinfo_t` de iPodLinux pone `sdram_size` en `+0x80`
mientras **este** firmware lee `+0xe0`, que cae dentro de un pad que iPodLinux
nunca decodificó. Del commit:

> *Filling the struct from it would be inventing with the appearance of rigour.*

**El matiz final** (`d83fd8908`): la hipótesis 2 de `288737573` —"la ROM deja un
bloque de parámetros"— **vuelve**, pero no como se enunció. Las dos lecturas
eran ciertas **a la vez**, que es exactamente por qué se sobre-rechazó:
`0x1084` sí es "dirección de la global N" **y** las entradas 24 y 28 de esa
misma zona sí son el par {tag, puntero} que iPodLinux llama SysInfo.

> **Lección**: rechazar una hipótesis por haber refutado **una de sus
> formulaciones** es un error de tipo específico. Aquí costó dos días de
> calendario.

### 5.6 El remap, correcto por accidente

`57ce76fab`. La regla vieja era "cualquier escritura a MMAP0 mete DRAM en la
dirección 0", y era **correcta por casualidad**. Los dos firmwares usan
ventanas distintas (documentado en `ipod_video.c:374-384`):

```
Rockbox  MMAP0  logical 0x00003e00  physical 0x10000f84   <- esta
Apple    MMAP0  logical 0x00003bf0  physical 0x00003a88
         MMAP1  logical 0x00003a00  physical 0x10000f84   <- esta
         MMAP2  logical 0x20003800  physical 0x00003f88
```

El MMAP0 de Apple mapea baja memoria a físico cero, que **no** es el remap; el
que cuenta es MMAP1. Funcionaba porque la escritura a MMAP0 disparaba la regla
antes de tiempo. Ahora se decodifica el par: base lógica cero + base física
`DRAM_MEM_BASE` (`ipod_video.c:419-425`).

El mismo commit corrige comentarios que habían quedado mintiendo:
`ipod_video_intc.c` decía que el COP no estaba modelado, y la cabecera de
`ipod_video.c` decía que sólo se crea un núcleo y que el remap no está
modelado.

> **Lección**: los comentarios envejecen y **el compilador no los revisa**. En
> este proyecto han mentido al menos tres veces; ver la nota de mantenimiento
> al final.

### 5.7 El PMU, y un fichero de registros que era peor que un error

`62af0171e`. El retailOS se paraba en `pmu_adc_read` (`0x28341c`), girando
**sin timeout** sobre el bit 7 de ADCS2 del PCF50605, porque `0x7000c000` era un
fichero de registros plano: cada transferencia "funcionaba" y devolvía el
último byte que el propio firmware había escrito.

Aquí se corrige **la corrección** de `a38f51d07`: el bloque no era ni I2C ni
click wheel, **era los dos**. I2C en `+0x00..+0xff`, click wheel de `+0x100`
hacia arriba, y comparten línea de interrupción. Está escrito en
`ipod_video.h:251-260`, con la advertencia textual: *"Which of the two owns the
window was got wrong once already, in both directions."*

Tres cosas que sólo el driver de Apple revela, y que Rockbox nunca habría
enseñado:

- Los bits 2:0 de STATUS son un **código de resultado**, no relleno (`0x282f44`
  los enmascara con 7, `0x28358c` aborta si no es cero).
- `+0x08` es un **segundo registro de dirección**, para el write-then-read
  combinado de `0x282e30`, cuyo byte recibido aterriza en DATA1, no en DATA0.
- **El bloque tiene que funcionar sin inicializar**: la rutina DEV_EN/DEV_RS de
  Apple (`0x282e98`) sólo se alcanza cuando una transferencia expira, así que
  si STATUS nunca dice BUSY el guest **no habilita el bloque jamás**.

**Efecto colateral que resultó ser un arreglo**: Rockbox pasó a arrancar en el
menú principal en vez de en *Now Playing*. `rtc_check_alarm_started()` decide
que lo despertó la alarma si reloj y alarma coinciden en diez segundos; sin
RTC, ambas lecturas devolvían lo mismo y Rockbox creía que había sonado una
alarma **en cada arranque**.

> **Lección**: un fichero de registros plano **no es un stub neutro**. Devuelve
> lo que el guest escribió, que es una respuesta *plausible*, y por tanto peor
> que un error: no hay log, no hay fallo, y el bucle no sale nunca.

### 5.8 El bit 31 — la vista sin caché

`0ee503c2e`. Data abort leyendo `0x93EE1B70`, que es DRAM normal
(`0x13EE1B70`) con el bit 31 puesto. Diez constructores de buffer comparten el
mismo idiom, y **la excepción es lo que identifica el bit**:

```
cmp  r1, r0, lsr #28      ; nibble alto 4, o sea ¿IRAM?
beq  done                 ; entonces déjalo
orr  r0, r0, #0x80000000  ; si no, así se usa el buffer
```

IRAM es la única memoria del chip que **nunca se cachea**, y es exactamente la
que el firmware se niega a marcar. Rockbox dice lo mismo en C con otra
constante (`system-target.h:164`, `UNCACHED_ADDR`). Razonamiento conservado en
`ipod_video.h:92-114`.

**Cómo se encontró**, y es reutilizable: breakpoint en el `memcpy` del firmware
por el gdb stub de QEMU, preguntando quién pasa un puntero origen con el bit 31
puesto → `0x21701c`; luego **watchpoint de escritura** sobre esa palabra del
descriptor → aterriza en el constructor.

---

## Fase 6 — El vídeo: la UI vive al otro lado del chip

`433be156f` implementa el handshake de bootstrap (`BCM_ALT_CONTROL` bits 7 y
6). Nota que explica por qué esto no salió antes: **Rockbox nunca llega ahí en
esta máquina**, porque `bcm_init()` sólo corre si `GPO32` bit 14 dice que el
chip está sin alimentar, y nosotros fingimos que lo está. El retailOS levanta
el chip en cada arranque. Eso expuso dos bugs del modelo:

- **Los registros de dirección son de 16 bits.** Apple los carga como dos
  halfwords, la baja primero (`0x287c28`); Rockbox siempre escribe la palabra
  entera por `BCM_WR_ADDR32`. Asignar el valor en cada escritura dejaba sólo la
  mitad alta — una subida a `0x000fc000` habría ido a `0x000f0000` **sin decir
  nada**. Ahora es un registro de desplazamiento cargado por arriba
  (`ipod_video_bcm.c:323-330`).
- **El puerto de datos es tan ancho como el acceso.** Una escritura de halfword
  que guardaba una palabra pisaba los dos bytes siguientes.

`fb7c1039a` modela el DMA del PP502x — **dos bloques idénticos**. `pp5020.h` de
Rockbox documenta sólo uno (maestro `0x6000a000`, canales `0x6000b000`,
`DMA_IRQ` 26) y usa sólo su canal 0 para el FIFO de I2S. El otro (maestro
`0x60008000`, canales `0x60009000`) **no aparece en ninguna fuente pública**:
es el que usa Apple para el vídeo. Los números de interrupción no son
conjeturas — `0x1d98d0` registra los handlers bajo los índices 27 y 28 del
firmware y la tabla de `0x1fcaa0` los mapea a las líneas 26 y 27; que el índice
27 caiga exactamente en el `DMA_IRQ` documentado de Rockbox es la comprobación
cruzada, y una máquina en marcha lo confirma: `CPU_INT_EN_STAT` lee
`0xcc002017` *(sólo commit)*.

`689c4892d` hace que el Broadcom conteste como si su firmware corriera. Cuatro
cosas que Rockbox solo nunca habría enseñado:

- El puerto de datos tiene que aceptar **bytes sueltos** (el descriptor DMA
  deja el campo de ancho a cero = 8 bits).
- **Un comando sólo cuenta si el puerto de escritura apuntaba al buzón**: una
  subida de 200 KB barre el puntero por encima de la dirección `0x1F8` del
  chip, y en el chip real el VideoCore está parado mientras se carga su propia
  imagen.
- Ambas palabras de control se escriben como dos halfwords, así que el efecto
  lateral se decide sobre la **palabra ensamblada**.
- Los puertos primario y alternado son **motores separados con punteros
  propios**; Rockbox sólo usa el primario, el retailOS usa los dos a la vez.

También corrige un bug propio: un comando terminado ahora sobrescribe
`BCMA_COMMAND` (que es lo que ambos drivers vigilan) en vez de poner a cero
`BCMA_STATUS`, que era justo el registro del que el retailOS saca su puntero de
interfaz.

Y un caso de **"publicar nada no es una opción"**: si no se publica estructura,
`0x288058` devuelve error, su único llamador lo ignora, y `0xcff8c` desbloquea
un mutex que la ruta fallida nunca creó → data abort. Hay que publicar la
estructura **con todos los bytes a cero**, que es la forma en que el driver
deletrea "sin canales".

### `5b5af8966` — el que dibuja

El hallazgo: **la UI de Apple no la pinta el PP502x**. El retailOS entrega
superficies enteras al BCM2722 y el procesador del BCM las compone. Sin canales
publicados, cada llamada de display se abandonaba antes de formatearse
(`0xe9380` devuelve -1 en cada comando ASCII, `0x286cf8` en cada RPC), **en
silencio**.

Se implementan dos canales: tipo 1 (vocabulario `gencmd` ASCII, con nombres y
plantillas sacados del `vmcs.bin` real del aparato) y tipo 2 (RPC binario de
display; opcode 8 crea superficie, opcode 3 la presenta). No se inventa ninguna
interrupción: cada petición es síncrona dentro de una tarea, y `0x2883d4` sólo
duerme sobre un anillo vacío.

Resultado: pantalla de idioma a 320×240 RGB565, **75.267 de 76.800 píxeles
encendidos (98,00 %)**, idéntica a 10, 20, 40, 60, 90 y 120 s. Reproducido en
la auditoría del 09-ago con `appletest.sh`.

### El último muro: `HI_IRQ`

`662d0fb83`, y es el ejemplo más limpio de todo el proyecto.

`pp5020.h` de Rockbox lista `HI_IRQ` como la fuente 30 del banco bajo, junto a
`TIMER1_IRQ 0` y `DMA_IRQ 26`, **y luego nunca la lee**: `system-pp502x.c:76`
en adelante consulta `CPU_HI_INT_STAT` directamente. Un modelo que deje la
línea 30 sin conducir **parece perfectamente correcto desde Rockbox**.

El retailOS hace lo contrario: su dispatcher de `0x277128` lee las dos palabras
de estado y **se niega a mirar el banco alto si el bajo no tiene el bit 30**.
Así que cada interrupción del click wheel se tomaba y la tiraba el propio
dispatcher del guest, y la UI de Apple se quedaba en la pantalla de idioma para
siempre **mientras Rockbox scrolleaba con las mismas teclas**. Documentado con
la traza en `ipod_video_intc.c:46-80`.

> **Lección**: un fichero de cabecera de terceros documenta el *hardware*, pero
> el **uso** que ese proyecto hace de la cabecera te dice qué partes están
> realmente ejercitadas. **Lo que un driver nombra y no usa es exactamente
> donde tu modelo tiene un agujero invisible.**

---

## Catálogo de hipótesis retiradas

Índice rápido, para no volver a proponerlas.

| # | Afirmación | Estado | Dónde |
|---|---|---|---|
| 1 | El alias de DRAM en `0x100000` causa la pantalla negra | Retirada por experimento | `6ea6f90ed` |
| 2 | La entrega de interrupciones causa la pantalla negra | Retirada por experimento | `6ea6f90ed` |
| 3 | Falta `CACHE_CTL_VECT_REMAP` → NOP sled | Implementada; ejecución idéntica; **no era** | `2893ab659` |
| 4 | "Aliasear DRAM bajo no ayuda" | **Experimento mal montado**: el alias iba en 0, no en `0x100000` | `90de12e29` |
| 5 | `0xe9b98` es la primera syscall del firmware | Falsa: es el impresor de panics de la libc de ARM. Sostuvo 3 commits | `d06ce3123` |
| 6 | El retailOS espera sus handlers ya en IRAM | Consecuencia, no causa: los instala él mismo en `0x2a0` | `d06ce3123` |
| 7 | 139 referencias a `0x40000000` en la imagen | Falsa: búsqueda de bytes crudos sobre tablas de coma flotante. Son **1** | `b9786a0bb` |
| 8 | El flash en blanco es el bloqueo; hace falta un volcado | Retirada dos veces; la tercera forma (SysInfo) se reproduce sin volcado | `d4259dea7`, `288737573`, `3b2637dc8` |
| 9 | El COP rellena la global del tamaño de RAM | Falsa: inferencia por proximidad, nunca verificada | `288737573` |
| 10 | `0x1084` es un puntero a bloque de parámetros de la ROM | **Sobre-rechazada**: las dos lecturas eran ciertas a la vez | `288737573` → `d83fd8908` |
| 11 | `PP_VER` debe decir `PP5022C` (lo que muestra Rockbox) | Falsa: el firmware quiere la forma `"PP5022A "` | `0763f94eb` |
| 12 | `0xf0c` + el comparador de cadenas eligen la base de globales | Falsa: `0xf0c` es código muerto en este build; decide `0x1084`→`0x598` | `57ce76fab` |
| 13 | Cualquier escritura a MMAP0 hace el remap | Correcta por casualidad; Apple usa MMAP1 | `57ce76fab` |
| 14 | El bloque `0x7000c000` es I2C | Corregido a "click wheel"… y **sobre-corregido**: son los dos | `a38f51d07` → `62af0171e` |
| 15 | Dejar el mágico SysInfo a cero es inofensivo | *"The earlier analysis had it backwards"* | `3b2637dc8` |
| 16 | `vmcs.bin` está en el offset `0xb27508` de la partición | El offset apuntaba **`0xBF08` bytes dentro** del blob | `8c873b597` → `c51e0f6fc` |
| 17 | El trabajo del Classic hay que escribirlo de cero | Falsa: `freemyipod/qemu` rama `s5l8702-dev` ya existe | `3f5cd7280`, `e17924eab` |
| 18 | El mapa del S5L8702 de `wiki/02` | Colaron bases de S5L8700/8701; "Mikey" no es el click wheel | `e17924eab` |

---

## Técnicas de depuración, por lo que rindieron

1. **Activar semihosting antes de trazar** (`d06ce3123`). Convirtió una
   pantalla negra en `Type: _sys_exit` con volcado de registros. Coste: una
   línea.
2. **Capturas a intervalos, no una sola** (`ad065fbeb`). Un fotograma no es un
   estado.
3. **Trazar dónde *entra* el flujo en zona muerta, no dónde sale**
   (`90de12e29`).
4. **`findlit.py`**: decodificar `ldr rD,[pc,#imm]` en vez de buscar bytes. 139
   falsos positivos → 1 verdadero (`b9786a0bb`).
5. **gdb stub: breakpoint en el memcpy del firmware + watchpoint de escritura**
   sobre el campo sospechoso (`0ee503c2e`).
6. **Volcar estado del guest desde `memory_region_access_valid`**: da el PC que
   falla y la dirección de retorno del llamador en una línea (`084445503`).
7. **`pmemsave` + búsqueda de cadenas en DRAM** para saber qué tareas llegaron
   a arrancar: los nombres copiados al heap del planificador distinguen "la
   tarea corrió" de "la cadena está en la imagen" (`8c873b597`).
8. **`-trace "ide_*"`** para separar fases de arranque (`ad065fbeb`).
9. **Dos trampas de coste alto**, en [05-build-notes.md](05-build-notes.md):
   `-d unimp` en un bucle apretado ralentiza lo bastante para parecer un
   cuelgue, y **las transferencias DMA no emiten `ide_sector_read`**, así que
   grepear eso no prueba nada.

---

## Patrones que se repitieron, y merecen ser regla

1. **El bit de estado que nunca llega.** Es el fallo más frecuente de todo el
   proyecto: `PLL_STATUS` bit 31, la caché `READY`, USB fuera de reset (`+0x28`
   bit 7), `BCM_ALT_CONTROL` bits 6/7, ADCS2 bit 7. Síntoma siempre idéntico:
   **bucle sin salida, sin log y sin fallo**. La sección "Registros con
   comportamiento" de [07](07-pp502x-hardware.md) existe precisamente porque
   ahí acabaron casi todos los bloqueos.
2. **Cero no es "desconocido inofensivo".** `PROCESSOR_ID` a cero no es ninguno
   de los dos núcleos; `CLICKWHEEL_DATA` a cero no es "sin botones" sino "sin
   paquete"; `PP_VER` a cero es una tercera respuesta sin caso; GPIO a cero
   significa "todo pulsado", porque se lee activo a bajo.
3. **Suelo de `unimplemented_device` bajo cada ventana de registros**, con lo
   modelado encima. Un registro desconocido que lee cero se sobrevive; uno no
   mapeado es un data abort cuyo handler también falla (`6702ab527`).
4. **Saltarse el cargador de ROM es contraer una deuda.** Hay que reproducir a
   mano todo lo que ese cargador dejaba en memoria: `GPO32` bit 14, el bloque
   SysInfo, el par {tag, puntero} de `0x4001ff18`.
5. **Criterio para saber qué venía de fuera**: un valor que el firmware
   **compara pero nunca produce**. `"IsyS"` se carga en una sola instrucción en
   7 MB, y sólo para compararlo.
6. **Marcar lo asumido en el propio código, no sólo en el commit.** El árbol
   usa `ASSUMED`/`INFERRED`/`UNDETERMINED` en mayúsculas junto al valor. Y el
   criterio negativo, que es la parte difícil: no rellenar `sysinfo_t` desde
   iPodLinux porque *"sería inventar con apariencia de rigor"*.
7. **Regresión automatizada desde pronto.** `navtest.sh` contra
   `assets/nav-baseline` y `appletest.sh` con umbrales en píxeles. Cada commit
   posterior reporta la diferencia, y las que se aceptan se explican.

---

## Cuestiones que siguen abiertas

Ninguna de estas está resuelta. Van aquí para que nadie las dé por cerradas.

### La talla de la DRAM (cuestión abierta)

`DRAM_MEM_SIZE` son **64 MB**, y conviven con **dos fuentes que dicen 32**: el
default interno del firmware (`fp = 0x700`) y la tabla de iPodLinux para
`hw_ver 0xb`. Las dos cifras pasan el assert y la máquina es autoconsistente
con cualquiera de ellas. Se dejó en 64 porque es lo que Rockbox lee del
silicio del aparato real. El razonamiento está en `ipod_video.h:31-43`.

### Lo demás

- **Qué significa el bit 26 para el controlador de memoria real.** El modelo
  funciona ignorándolo; nadie ha confirmado qué hace el hardware
  (`INFERRED`, `2b1ba27e3`).
- **El campo `mask` del remap** está documentado pero **no aplicado**
  (`ipod_video.c:388-391`). Un guest que pida una ventana distinta no la
  obtendrá.
- **El valor 390 del ADC de Apple** y **el ID `0x00` del PCF50605** están
  marcados `ASSUMED` en el código; no hay fuente que los confirme.
- **Dónde vive la interfaz de host del VideoCore** dentro de la memoria del
  chip es una elección del modelo (`BCM_IFACE_BASE`, `ASSUMED`).
- **Si el BCM2722 real tiene 2 o 4 MB de SDRAM**: elección nuestra, marcada
  `UNDETERMINED` en `ipod_video_bcm.h:33-40`.
- **Por qué la rueda no mueve el resaltado en la pantalla de idioma del
  retailOS.** Medido (0 px de diferencia), no diagnosticado: no se sabe si es
  limitación del modelo o comportamiento del propio firmware.

---

## Nota de mantenimiento

Tres veces en 51 commits un comentario del código ha quedado mintiendo después
de que el código cambiara debajo (`57ce76fab` arregló dos de golpe), y la wiki
ha ido por detrás del árbol al menos otras tantas. Si tocas algo de esto:

- Los comentarios de cabecera de `ipod_video.c` enumeran **lo que no está
  modelado**. Esa lista caduca con cada commit que modela algo.
- El mapa de memoria de [07](07-pp502x-hardware.md) y la lista "modelado hasta
  ahora" de [04](04-roadmap.md) son los dos sitios que más se quedan atrás.
- Las secciones de diagnóstico que quedan superadas **no se borran**: se marcan
  con un banner de *superado* y un enlace a lo que las sustituye. Ese es el
  valor de este documento.
