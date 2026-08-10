# Historia técnica del port del iPod classic 6G

Cronología de cada obstáculo que hubo que quitar para que un iPod classic 6G
emulado arrancara el retailOS original de Apple y llegara a una pantalla que se
puede navegar, **incluyendo las hipótesis que resultaron falsas**. Igual que en
[09-historia-5g.md](09-historia-5g.md), las equivocaciones ocupan aquí tanto
sitio como los aciertos, y a propósito.

Los otros documentos dicen **cómo es** el hardware
([02](02-s5l8702-hardware.md), [08](08-s5l8702-fuentes-externas.md)), **qué
hace el firmware** ([11](11-classic6g-retailos.md)) y **cómo se sacó del
aparato** ([10](10-classic6g-emcore.md)). Este dice **cómo se averiguó**, y qué
costó.

El capítulo que más se ha ganado el sitio está en medio y no al final: **[tres
veces un descarte resultó ser la pista buena](#tres-veces-un-descarte-resultó-ser-la-pista-buena)**.

## Cómo leer esto

Cada afirmación lleva el hash del commit donde se hizo y, cuando el resultado
quedó fijado en el árbol, el fichero. Tres niveles de confianza, y se
distinguen siempre:

- **Verificado en el árbol**: leído en el código actual de
  `hw/arm/ipod_classic/` e `include/hw/arm/ipod_classic/`, o ejecutado.
- **Reproducido en auditoría**: alguien que no lo escribió volvió a medirlo
  desde cero. Es el nivel de las cifras de pantalla (75 463 píxeles, 8 000 por
  giro) y del desensamblado de las direcciones clave.
- **Sólo en el mensaje de commit**: cuentas de bloques traducidos y direcciones
  del desensamblado que no se han vuelto a comprobar una por una. Van marcadas
  *(sólo commit)*.

`assets/` está fuera de git — el firmware no es nuestro para redistribuirlo —,
así que aquí no hay capturas: hay **cifras medidas y la orden que las
regenera**.

## Las jornadas

27 commits tocan el Classic sobre el merge upstream `328815631`, del 6 al 10 de
agosto de 2026, entrelazados con los del iPod Video 5G. La cuenta se saca así:

```bash
git log --oneline 328815631..HEAD -- hw/arm/ipod_classic include/hw/arm/ipod_classic \
    tools/ipod_classic wiki/0{1,2,3,8}-*.md wiki/1{0,1}-*.md | wc -l
```

| Fecha | Commits | Qué se consiguió |
|---|---|---|
| 06-ago 14:04–14:30 | `994f52fde`, `2ddc18b2a` | Máquina `iPod-Classic`: CPU, RAM, VICs, timers y una ruta de interrupción verificada con test bare-metal. Y el muro: el `osos` está cifrado |
| 08-ago 19:47 | `3f5cd7280`, `e17924eab` | Dos correcciones que ahorran trabajo: ya existe un modelo QEMU del S5L8702, y el mapa de [02](02-s5l8702-hardware.md) colaba bases de otro chip |
| 09-ago 15:31 | `b71d16f3d` | **El bloqueo fundacional cae**: el `osos` en claro, descifrado por el propio aparato |
| 09-ago 16:34–17:05 | `e7f8bfaf3`, `1eb53f6ff` | Del reset al bucle idle del kernel RTXC, y de ahí al disco |
| 09-ago 18:20–19:36 | `8edf7fcdd`, `b675f32cc`, `88343aa0a`, `b9c2b38a7`, `c54ac2011` | Tres giros sin salida, el ciclo de caída sin disco, y ATA con DMA leyendo de verdad |
| 09-ago 22:24–23:15 | `02c6f3327`, `8395ac19d`, `b5026710f`, `70c29550a`, `61c92adf4`, `4bf80dacd` | El panel, el pin 86 del GPIO, `gdbpoke.py`, y la auditoría que retira un descarte |
| 10-ago 01:01–01:49 | `3e67e5bbc`, `85450446c`, `03421c85d` | **Hay imagen**: la pantalla de selección de idioma |
| 10-ago 03:26–03:58 | `8ecd4e673`, `c33634b69`, `59cf47aff` | **Se navega**: la rueda mueve el resaltado y el botón central entra |

---

## Fase 0 — Una máquina que ejecuta, y un firmware que no se puede leer

`994f52fde` monta lo mínimo: ARM926EJ-S, SDRAM en `0x08000000` (64 MB), IRAM0 e
IRAM1 en `0x22000000` y `0x22020000`, VIC0/VIC1 (los PL192 de serie de QEMU) en
`0x38E00000`/`0x38E01000`, UART0 por `exynos4210_uart`, y **32 periféricos
registrados como `create_unimplemented_device()`** para que `-d unimp` diga a
cuál va el huésped a continuación. El mapa sale de preprocesar
`firmware/export/s5l87xx.h` de Rockbox para `CONFIG_CPU == S5L8702`.

`2ddc18b2a` escribe el bloque de timers entero y verifica una interrupción de
punta a punta con un test bare-metal (`tools/ipod_classic/tests/`): timer B a
10 kHz como Rockbox, tabla de vectores en `0x0`, IRQ 8 desenmascarada en VIC0.
El log de `-d unimp,guest_errors` sale vacío.

Y ahí se acaba lo fácil. El `osos` del `.ipsw` está cifrado con la clave GID del
SoC. La medida que lo zanja es de `d9cf852ab`, la misma que decidió mudar el
proyecto al iPod Video 5G: **entropía de 7,99 bits/byte y cero cadenas ASCII
legibles**, contra 5,2–6,3 bits/byte y 21 016 cadenas en el firmware del 5G.

> **Lección, heredada del 5G**: la entropía por byte de un volcado es la prueba
> de cinco segundos de si vas a poder trabajar con ese firmware. Antes de
> planificar nada, mídela.

### Dos correcciones tempranas, las dos de coste negativo

`3f5cd7280`: **ya existe un modelo QEMU del S5L8702**, en `freemyipod/qemu` rama
`s5l8702-dev`, descendiente del mismo `devos50/qemu-ios`. La wiki decía que
había que escribir ATA, rueda y LCD desde cero. (Al final **se escribieron desde
cero igualmente**, desde el desensamblado de Apple y los drivers de Rockbox; si
ese fork llegó a consultarse no consta en ningún commit. El aviso sigue en
[03-reuse-qemu-ios.md](03-reuse-qemu-ios.md).)

`e17924eab`: el mapa de [02](02-s5l8702-hardware.md) se había derivado
preprocesando `s5l87xx.h` y **colaron bases que sólo existen en S5L8700/8701**
(`SPI0 0x3CD00000`, `ADC`, `RTC`, `FMC`, `USB host/function`, `INT_BASE`,
`MS_BASE`), y llamaba *click wheel* a **"Mikey"**, que es en realidad el
micrófono y el mando del jack de auriculares, en I²C0 a `0x72`. Las correcciones
viven en [08](08-s5l8702-fuentes-externas.md).

---

## Fase 1 — El bloqueo fundacional: que descifre el propio aparato

`b71d16f3d`, y es el commit del que cuelga todo lo demás.

**La idea que lo desbloquea no es criptográfica, es de logística**: la clave GID
está fundida en el silicio y no se puede extraer, pero no hace falta extraerla
— basta con **pedirle al aparato que descifre**. Un iPod classic 6G físico
corriendo emCORE expone su motor AES por USB. El `osos` entero sale en claro en
**1,2 s**.

Tres obstáculos por el camino, todos en [10](10-classic6g-emcore.md):

- **Ninguno de los dos clientes oficiales funciona.** El de HEAD sólo acepta
  interfaces `ff/00/00` (este aparato es `ff/ff/ff`) y usa EP0, protocolo
  posterior a r891; el de r859 llama a pyusb con el parámetro `interface`, que
  desapareció en pyusb 1.0.0b1. De ahí
  `tools/ipod_classic/emcore_s5l8702.py`, que habla el protocolo de r859
  directamente y sólo implementa órdenes de lectura más lo justo para el AES.
- **El detalle que hace falta para trocear imágenes grandes**: `hwkeyaes()` pone
  el IV a cero al principio de **cada llamada**, así que el primer bloque de
  cada trozo sale mal. En CBC eso se repara exactamente con un XOR contra el
  último bloque de cifrado del trozo anterior. Comprobado contra el aparato y
  con test offline (`tools/ipod_classic/test_emcore_s5l8702.py`).
- **La bootrom no se puede leer.** Hallazgo negativo, y ahorra tiempo a quien
  venga: tras el arranque, `0x20000000` devuelve una constante por banco de
  16 KiB. Está bloqueada y por esta vía no hay volcado.

Verificación de que el claro es lo que decimos: vectores ARM coherentes,
`"Copyright 2001-2008 Apple Inc."`, la interfaz literal (*Cover Flow*, *Now
Playing*), `RTXC v3.2b`, y las rutas de compilación de Apple dentro del `aupd`.
**42 014 cadenas ASCII frente a 0.**

> **Lección**: cuando el secreto vive en el silicio, la pregunta útil no es
> «¿cómo saco la clave?» sino «¿quién más tiene ya acceso a ella?». La respuesta
> era el propio aparato, encima de la mesa.

---

## Fase 2 — Del reset al bucle idle: cuatro cosas, y una de ellas era un `abort()`

`e7f8bfaf3`. Cada cambio quita exactamente una cosa que paraba al `osos` antes
de llegar al kernel RTXC.

**Un modelo de reloj propio.** La máquina había tomado prestado
`hw/arm/ipod_touch_clock.c`, que es el mapa del S5L8900. Los dos coinciden hasta
`0x4c` **por casualidad**, pero el S5L8702 tiene `SWRCON` en `0x50`, `RSTSR` en
`0x54` y `PLLMOD2` en `0x60`, y el modelo prestado contestaba a esos tres con
`hw_error()` — que no es un log, es un `abort()` del proceso entero. El `osos`
escribe los tres.

**El bloque de parámetros del bootrom.** `0x0827a904` es el accesor completo:
«devuelve `0x2203ff00 + N`», los últimos 256 bytes de IRAM1; `0x08323aec` es su
único llamador, mágico `'IsyS'` en `+0x18`, puntero en `+0x1c`, `memcpy` de
`0x120` bytes. Sin ese bloque el firmware **no se caía: se reiniciaba**.
`0x083286d4` lee el id de hardware y `0x0827a188` exige `(id >> 16) == 19`; con
cualquier otra cosa el firmware escribe `"softupdt"` en `0x2203ff00` y tira de
`SWRCON`. Eso era la escritura de `0xaa5` que se veía y no se entendía.

Y el `19` no es una conjetura: **es lo que dice la entrada `HwVr` del bloque
SysCfg de la NOR de este mismo aparato**, en el offset `0x5c` de
`assets/classic6g/bootflash_nor_1MB.bin`. Se escriben tres campos y ni uno más
(id de hardware, base y tamaño de DRAM); los otros ocho que el firmware lee
quedan a cero, porque una invención plausible no se distingue de un dato.

**Semihosting encendido por defecto**: 131 sitios con `svc 0x123456` en la
imagen, 115 precedidos de `mov r0,#4` (`SYS_WRITE0`), más el `RTXCbug` de RTXC
con su prompt en `0x0827bd79`. El mismo cambio de una línea que dio la vuelta al
port del 5G.

**Un suelo de E/S sobre los 768 MB de `0x38000000..0x3FFFFFFF`**, por debajo de
los bloques con nombre. El firmware mapea toda esa ventana como una sección
identidad sin caché en la tabla de páginas que construye en `0x22004534`, así
que un registro que nadie haya listado **no es un cero inofensivo: es un data
abort**, tomado por un manejador que también falla.

**Resultado trazado**: la imagen se relocaliza, enciende la MMU, corre `main`,
levanta el kernel RTXC, desenmascara interrupciones y se sienta en el bucle
`mcr p15,0,rX,c7,c0,4` de `0x22003b1c`, con **6 528 interrupciones de timer en
20 s**. Se para ahí porque una tarea está bloqueada en una transferencia I²C al
PMU de `0xe6` que nadie contesta.

---

## Fase 3 — I²C y el PMU: dos registros que parecen uno

`1eb53f6ff`. `0x08325a20` escribe `IICCON`, `IICDS` e `IICSTAT = 0xf0` y espera
el evento RTXC 61 en un bucle sin salida; sólo su manejador de interrupción
`0x0832566c` publica ese evento.

Lo que hace que este bloque no sea el S3C24xx al que se parece lo dice Rockbox
en `s5l87xx.h`, y el driver de Apple coincide **en todos los puntos**, que es lo
que lo hace seguro:

- `IICCON[13:8]` habilitan interrupciones; `IICSTA2[13:8]` **leen el estado y
  las limpian escribiendo**.
- `IICCON[4]` no limpia interrupciones, y en modo maestro el byte siguiente
  arranca al escribirlo como 1.

Es decir: **control de flujo y estado de interrupción viven en registros
distintos**, y un modelo que los ponga en el mismo cuelga al huésped. El
manejador de Apple acusa con `IICSTAT2 |= 0x100` y reanuda con el mismo idiotismo
`IICCON = IICCON` que Rockbox escribe en `i2c_wr_internal()`.

**Un byte tarda 25 µs virtuales, no cero.** Los dos drivers escriben `IICDS`,
luego `IICCON` para arrancar el byte, y **sólo entonces** limpian la
interrupción anterior en `IICSTAT2`. Completar el byte de forma síncrona hace
que esa limpieza borre la interrupción que la escritura acababa de levantar.

El PMU es un **PCF50635 en `0xe6`**, con puntero de registro que sobrevive a un
STOP — y eso no es una suposición: los dos drivers mandan el número de registro
en una transacción y leen en la siguiente.

**Una inferencia, y es lo bastante crítica como para tener nombre de
propiedad**: el retailOS **nunca habilita su propia interrupción de I²C**.
`0x08325908` es el único código que escribe `IICCON = 0x184`, y a él sólo se
llega cuando `0x220003b8` reporta el bus atascado 100 ms, o sea como recuperación
de bus. Todas las demás escrituras preservan lo que ya había. El driver **hereda
el habilitador de quien corrió antes**, y lo único que corre antes es la
bootrom, que tiene que hablar con este mismo PMU para saber por qué despertó la
máquina. Así que la máquina entrega el bloque con `IICCON = 0x184`. Marcado
`INFERIDO`: no hay volcado de bootrom, y [10](10-classic6g-emcore.md) dice que
este SoC no lo va a soltar.

**Resultado**: el retailOS pasa el PMU, lee el *strap* de panel de `PDAT(6)`,
configura GPIO, levanta la rueda de clic en `0x3C200000`, programa el
controlador de interrupciones externas y arranca el ATA. Ahora gira en
`ATA + 0x78` esperando un disco que no está.

---

## Fase 4 — Tres giros sin salida, y un ciclo de caída que era invisible

`8edf7fcdd`. Tres bloques, y **ninguno era un valor equivocado: los tres eran
bucles sin salida**.

**`0x3C100000`, el bloque que ninguna fuente nombra.** El único `0x3C100000` de
Rockbox es `LCD_BASE` bajo `#if CONFIG_CPU==S5L8700` — otro chip —, y freemyipod
no tiene nada ahí. La especificación entera es el código de Apple, y es lo
bastante pequeña para citarla completa: buscando el literal y después **todos**
los `LDR` relativos a PC que resuelven a una de esas palabras salen **exactamente
tres sitios**, así que la lista es completa y no «lo que apareció». Siembra en
`+8`, un cero en `+0`, y `while (!([+0] & 7)) ; return [+4]`. Envuelto en una
sección crítica que saca veinte palabras y descarta diecinueve: eso es sembrar
un generador y tirar sus primeras salidas, y nada más encaja. Identidad
`INFERIDA`, contrato de tres registros **verificado**.

Y la consecuencia que hay que escribir en voz alta: **no hay bit de
habilitación**. La única escritura a `+0` es un cero, y ocurre **antes** de las
veinte lecturas. Un modelo que condicione «hay dato listo» a que el huésped haya
habilitado el bloque se cuelga en la primera extracción.

**El puerto de órdenes del LCD en `0x38300000`.** Rockbox y el retailOS giran
sobre los mismos tres bits de estado, que es lo que los hace fiables: bit 1
ocioso, bit 4 ocupado, bit 0 dato de lectura válido; `s5l87xx.h` anota el valor
de reset como `0x0106`. `lcd-s5l8702.c:207` y `0x080b6f98` del retailOS son el
mismo bucle instrucción por instrucción. Con la ventana sin implementar, los
`while (!(LCD_STATUS & 0x2))` no vuelven nunca.

**El *task file* del ATA en `0x38700000`**, sólo PIO, para que el sondeo de
almacenamiento termine.

### Y el hallazgo: el firmware llevaba todo el rato diciéndolo

Sin disco la máquina **no se quedaba ociosa: se caía y reiniciaba**, diecinueve
veces cada veinte segundos — 19 *Data Abort*, 19 llamadas de semihosting, 19
escrituras de `SWRCON`. Era invisible porque el diagnóstico sale por **stderr** y
las trazas lo tiraban:

```
CPU_exception_handler
warning: S5L8702: guest asked for a software reset (SWRCON = 0xaa5), next image "--------"
```

La causa está en `DFSR 0x5 DFAR 0xEA00002A`. **`0xEA00002A` no es una
dirección: es una instrucción de salto ARM.** Y es `0xEA000002 + 0x28`, donde
`0xEA000002` es exactamente la palabra que `main` escribe en `[0x22000000]`. Lo
que aborta es un par de saltos de C++ — carga de vtable y método `+0x28` — o
sea **una llamada virtual sobre un objeto nulo**.

Lo interesante, y es específico de esta máquina: como el propio firmware mapea
VA 0 sobre IRAM0, **la desreferencia nula no falla**. Lee tranquilamente una
palabra de la tabla de vectores y revienta la *segunda*, doscientos bytes más
tarde y en otro sitio.

> **Lección, y es la número 1 del proyecto entero**: si el firmware imprime
> algo, escúchalo. El semihosting ya estaba encendido y aun así se estuvo un
> rato mirando histogramas de `-d unimp` en vez de leer las dos líneas que lo
> explicaban todo.

> **Lección de diagnóstico reutilizable**: un `DFAR` con pinta de instrucción
> ARM (`0xEAxxxxxx`) es la firma de un puntero nulo en una máquina donde VA 0
> está mapeado.

---

## Fase 5 — ATA con DMA, y el orden que da forma al modelo

`b675f32cc`. La tarea de almacenamiento estaba bloqueada en una transferencia
que no podía terminar: programaba `ATA_XFR_NUM` y esperaba la interrupción de un
motor que guardaba sus descriptores y no movía nada.

El protocolo está **especificado dos veces y las dos coinciden**: Rockbox lo
escribe entero en `storage_ata-6g.c:851-880`, y el driver de Apple se trazó
haciendo lo mismo registro por registro. Eso es lo que hace que los significados
de los bits sean seguros y no plausibles.

**El orden es lo que da forma al modelo, y es el contrario del intuitivo.** La
dirección del búfer se programa **después** de la orden ATA, así que `start_dma`
no puede saber dónde va el dato: sólo puede quedarse con el *callback* que le da
el núcleo IDE, y es la escritura del bit de arranque la que dispara. (El PP502x
del 5G necesita la misma partición por la razón espejo: allí la dirección va
primero y el bit de arranque sigue yendo el último.)

Dos trampas que cuestan una tarde cada una, las dos ya pagadas en el 5G y
repetidas aquí:

- **`s->bus.dma` hay que reclamarlo después de `ide_bus_init_output_irq`**, no
  antes: lo último que hace esa función es poner `bus->dma = &ide_dma_nop`.
  Reclamarlo antes hace que toda orden de DMA se acepte y se ignore en silencio,
  que se parece muchísimo a un disco que no contesta.
- **Comprobar el DMA por la ausencia de quejas no vale.** Se comprueba leyendo
  la memoria del huésped: tras la primera transferencia, `0x0be5d3e0 + 0x1fe`
  contiene `55 aa`, la firma del MBR del sector 0 de la imagen, en el byte que
  le toca.

`ATATRACE=1` vuelca cada acceso a registro. Es como se leyó la secuencia del
huésped en primer lugar, y es barato de mantener.

Y la medida que reordenó toda la investigación: **el parser de la partición de
firmware no se ejecuta nunca**, con partición real en el disco o sin ella. Ni
él ni el código de interfaz que dibuja. Algo aguas arriba de los dos está
esperando.

> **Matiz que llegó después** (`b9c2b38a7`): esa medida se hizo con
> `grep -c '^0x08057160'` sobre un log de `-d in_asm`, y **`0x08057160` no es
> entrada de bloque**, es un `add r0,sp,#4` en mitad de la función. Habría dado
> cero aunque el código corriera en cada arranque. La entrada real es
> `0x0805712c` (`push {r1..fp,lr}`) y por ahí la conclusión se mantiene — pero
> la conclusión era correcta por otro camino, no por la medida. Ver la trampa
> de `-d in_asm` en [11](11-classic6g-retailos.md), §7.

---

## Fase 6 — El panel, un pin, y un depurador

`02c6f3327`. Dos dispositivos y una herramienta, los tres empujados por medida.

**El panel.** `ipod_classic_lcd.c` aceptaba órdenes y palabras de píxel y las
tiraba, así que nada de lo que hiciera esta máquina podía aparecer en una
pantalla. Ahora lleva una memoria de 320×240 en RGB565, ventana con `0x2A`/`0x2B`,
puntero de escritura tras `0x2C`, y un `qemu_console`. **Ninguna cifra viene de
un datasheet**: el juego de órdenes de 8 bits lo prueba el `LCD_CON = 0x80000c21`
que escribe el huésped y la única rama de `0x080b6f8c` que puede producirlo; la
geometría, los parámetros de `0x2A`/`0x2B` y el `(320<<16)|240` de `+0x74`.

Y se valida con un **caso de control**, porque un panel que no se prueba no es
una medida: `tools/ipod_classic/lcdtest.S` emite esa secuencia y empuja 76 800
palabras. `screendump` + `pixcount.py` devuelven **76 800 de 76 800 píxeles y
1920 colores**, que es al píxel lo que el test escribe.

**El pin 86 del GPIO.** `0x080aeca4` es literalmente `gpio_get_pin(86)`, y la
tarea de almacenamiento lo convierte directamente en «monta el volumen» o «no
hay medio». Con `0x3CF00000` sin implementar leía cero, así que **todo arranque
con disco le decía al gestor de volúmenes que no había medio**. Ahora el pin
sigue a si hay `-drive`.

**`gdbpoke.py`**, y esto cambia el ritmo de trabajo. `-d in_asm` sólo responde
«¿se tradujo un bloque que empieza aquí?»; la pregunta que hacía falta era «¿qué
hay en `r0` cuando el huésped llega a esta dirección?». Esta WSL no tiene
`gdb-multiarch`, así que hay un cliente del protocolo remoto de GDB de 200
líneas que habla contra el `-gdb tcp::PORT` de QEMU.

> **Trampa que costó una hora.** QEMU **no** tiene lógica de «ignora el
> breakpoint del que acabo de salir»: `accel/tcg/cpu-exec.c:346` dispara siempre
> que `pc == bp->pc`. Un `c` pelado desde una parada vuelve a parar en el mismo
> sitio **para siempre**, y la primera versión de la herramienta sacó ocho
> paradas idénticas de una función con 374 llamadores sin que saltara ninguna
> alarma. La salida está tres líneas más arriba en el mismo fichero:
> *«Singlestep overrides breakpoints»*.
>
> **Y el caso de control que lo destapó**: poner el breakpoint en una función
> con muchos llamadores y mirar si `lr` cambia entre paradas. Si no cambia, la
> herramienta está rota, no el firmware.

### La corrección de fondo: no hay ninguna tipografía dentro de la imagen

Con el depurador, el ciclo de caída sin disco se traza hasta su raíz, y **no es
lo que decía la wiki**. La búsqueda del recurso `"FONT"` **encuentra**
(`r0 = 0x086aef48`), la de `"Str "` también, y el registro que devuelve, leído
del huésped, es `0dad0001 0000003a 01d58101 0dad0002` — id de nombre, cuerpo 58,
estilo. La cadena de proveedores funciona.

El nulo aparece al **instanciar la fuente**: `0x082393ec` la busca en dos listas
que sólo llena `0x08239220`, y esa función **enumera el directorio
`"Resources/Fonts"` de un volumen montado** (`0x08239134` es un iterador de
directorio de verdad: salta las entradas que empiezan por `.` y llama a
`0x08107184` por cada nombre). Sin volumen, las listas están vacías, no casa
ninguna fuente, y el llamador llama a un método virtual sobre `{0,0}`.

Y no se puede esquivar, lo cual lo cierra: con `gdbpoke.py --skip` se salta esa
llamada sin tocar la imagen de Apple y **el arranque vuelve a caer**, ahora con
`DFAR 0xEA00003E` — otro método virtual, ranura `+0x3C`, sobre otro nulo del
mismo origen.

Medido después de todo esto: el retailOS **sigue dando 0 píxeles**, con disco y
sin él. La pantalla negra pasa de ser un encogimiento de hombros a ser un
número.

---

## Fase 7 — La auditoría que devuelve un sospechoso a la lista

`70c29550a`. Se vuelve a medir todo desde cero y, además de confirmar lo grande,
saca cuatro correcciones. La que importa es la primera, y tiene capítulo propio
más abajo:

**`0x38900000` estaba dado por descartado** con la prueba «ningún registro
recibe nada con pinta de dirección de DRAM». La prueba era **cierta**, y no
significaba lo que parecía. En un arranque sin disco ese bloque recibe **793
direcciones distintas**, y dos de ellas son inconfundibles:

| Registro | Valor | Qué es |
|---|---|---|
| `+0x400`…`+0xFFC` | `valor == offset − 0x400` | **768 palabras = 3 × 256: una tabla de gamma identidad**. Sólo un camino de vídeo escribe eso |
| `+0x214` | `0x013f00ef` | **la geometría del panel al píxel**, `(319<<16)|239` |

Y catorce de los dieciocho literales `0x38900000` de la imagen viven en
`0x08127xxx`, la misma función que escribe `LCD[0x74]` y `LCD[0x70]`. Descarte
retirado.

En la misma auditoría, y a ocho instrucciones de código que ya estaba
documentado: `0x08127f14` carga `[0x089115e4]`, lo compara con cero y llama a su
método `+0x0c` con `(0,0,0,320,240)` **antes** de arrancar el motor de trama.
Ese puntero vale `0x08911fe0` con la máquina corriendo — no es nulo — y su
`+0x0c` es `0x080c1164`, la función que este árbol ya había identificado como el
`displaylcd_setup()` de Rockbox. **O sea que el retailOS sí pide un dibujado de
pantalla completa**; esa llamada sólo fija la ventana, y los píxeles nunca iban
a venir por la CPU.

---

## Fase 8 — La imagen: hacían falta dos cosas a la vez

`3e67e5bbc`. Tres cosas tenían que ser ciertas al mismo tiempo, y las dos
primeras ya se sabían.

### 1. `0x38900000` es el compositor

Nada fuera del código de Apple nombra este bloque, así que el mapa de registros
entero de `ipod_classic_disp.h` sale de desensamblar **los veintisiete sitios
que cargan el literal `0x38900000`**, comprobados línea a línea contra un
arranque. Dos métodos, y se necesitan los dos:

1. **Los veintisiete literales** dan casi todo el mapa, y la lista es completa
   por construcción, no «lo que apareció».
2. **`DISPTRACE=1` con el PC del huésped** encuentra lo que el paso 1 no puede:
   `+0x200..+0x214` se escriben desde `0x081273f8..0x08127428`, que llega al
   bloque por `r6` y no por un literal propio.

La lectura honrada importa por sí sola, y hay medida antes y después: **seis
funciones hacen lectura-modificación-escritura sobre `+0x008`**, y mientras el
bloque leía cero cada una borraba lo que había puesto la anterior. Antes:
`+0x008` recibía `0x01110001`, luego `0x00008000`, luego `0x00000100`, luego
`0x40000000`, cada uno aterrizando sobre un cero. Después: `0x01118001`,
acumulado, que es lo que esas cuatro llamadas deben sumar.

Los píxeles salen de **los cinco descriptores de capa** en `+0x058 + k*0x18`,
cada uno con stride, formato, dirección de origen, tamaño y posición. Un
arranque real programa dos: una superficie de 320×240 a 32 bits en `0x09767a24`
y un panel de 108×240 a 16 bits en x=212 — y `212 + 108 = 320` al píxel.

**Y ahí estaba la imagen todo el tiempo**: `pmemsave 0x09767a24 0x4b000` y
mirarlo como 320×240 de 32 bits da la pantalla de idioma entera, ya compuesta.
No había que inventar nada; había que copiarla al panel.

Dos detalles que muerden:

- **`W` y `H` son con signo.** Mientras el panel derecho se desliza fuera de la
  pantalla el firmware escribe `+0x07c = 0xffc900f0`, o sea anchura −55. Leerlos
  sin signo dibuja 65 481 columnas de basura.
- **El orden de bytes es lo único `INFERIDO` del camino a píxeles.** Se lee la
  capa de 32 bits como ARGB8888 *little-endian* y la de 16 como RGB565, porque
  `config/ipod6g.h:85` de Rockbox dice `LCD_PIXELFORMAT RGB565` para este
  aparato, porque sólo así coinciden las dos capas, y porque entonces la barra
  de selección sale `#0484da` — el azul de Apple — y no `#da8404`, un naranja
  que no aparece en ninguna parte de esta interfaz. Es un argumento sobre los
  colores de Apple, no un datasheet, y está marcado como tal.

### 2. DMAC0 y DMAC1 son PL080, y QEMU ya traía el modelo

**Esto es lo que destraba el arranque con disco, y corrige la fase 5.** El ATA
nunca fue el problema: sus dos lecturas por DMA terminan y su semáforo despierta
a quien esperaba. Lo que no vuelve es un `memcpy` **posterior**, por DMAC0 canal
0, que duerme en el semáforo del canal con `KS_wait` **sin timeout**. El único
que puede levantarlo es la interrupción de fin de cuenta de DMAC0, `0x220027b4`,
registrada en `0x080357f4` sobre **IRQ 16**. Y `0x38200000` era
`create_unimplemented_device`.

Además hacía daño en silencio: `0x22004b68` hace lectura-modificación-escritura
sobre `C0Control`, y con el bloque devolviendo cero la escritura final salía
`0x0c000000` en vez de llevar el bit `I`, los anchos y la cuenta. **Un modelo
que sólo generase la interrupción sin devolver bien las lecturas seguiría
fallando.**

Dos fuentes independientes coinciden: Rockbox nombra los dos PL080
(`dma-s5l8702.h:33-36`) y los pone en IRQ 16 y 17 (`s5l87xx.h:1893`,
`#define IRQ_DMAC(d) (16+(d))`), y los desplazamientos que usa el retailOS
cuadran byte a byte con `hw/dma/pl080.h`.

Detalle de cableado que no es opcional: **el puerto maestro tiene que ser la
memoria del sistema**, porque el huésped programa los canales con el alias
cacheado (`0x0be55320`) mientras la CPU corre sobre el no cacheado
(`0x8be55320`).

### 3. Y con eso, la cadena entera

`-d in_asm` con disco, contando sólo prólogos de función verificados. **Las
nueve direcciones daban cero en todas las trazas anteriores del proyecto**:

| Dirección | Qué es | Antes | Ahora |
|---|---|---|---|
| `0x080aee04` | parser de MBR | 0 | **1** |
| `0x0808f8f4` | localizar la partición de firmware | 0 | **1** |
| `0x0805712c` | **el parser `]ih[`** | 0 | **1** |
| `0x08040cf8` | montar `rsrc` como volumen 4 | 0 | **1** |
| `0x08122a04` | levantar la bandera de almacenamiento | 0 | **1** |
| `0x081228d0` | la **salida** de `while (![obj+0x28]) wait();` | 0 | **1** |
| `0x08239134` | iterar `Resources/Fonts` | 0 | **1** |
| `0x08239658` | construir el objeto de cadena | 0 | **1** |
| `0x0815b1b8` | el código de interfaz que dibuja texto | 0 | **1** |

**Resultado**: `screendump` + `pixcount.py` pasan de 0 a **75 463 píxeles no
negros de 76 800, en 404 colores**, y la imagen es la pantalla de selección de
idioma del iPod — **mirada, no sólo contada**: barra «Language» con batería y
candado, nueve escrituras distintas (japonés, griego, checo…), barra de
selección azul sobre *English*, y el panel *Welcome / Bienvenue / Willkommen*.

La prueba independiente de que las tipografías vienen del disco es que **hay
texto en nueve escrituras**: esas fuentes sólo existen en `\Resources\Fonts\` del
volumen `rsrc`.

### El experimento de control que faltaba

`03421c85d`. Que el compositor hiciera falta es evidente — es lo único que
dibuja. Que hicieran falta **también** los PL080 era una inferencia sobre una
cadena larga. Se comprobó quitándolos:

> Con el compositor puesto y los dos PL080 fuera, arranque con disco de 45 s:
> **0 píxeles, 0 colores, y ni una sola línea de `SOURCE ADDRESS`.** El
> compositor está ahí y nadie le da nunca una dirección de origen.

(El parche era un `getenv` provisional; se revirtió y no está en el árbol.) Con
eso, la fase 8 deja de ser una historia bonita y pasa a ser una causa.

---

## Fase 9 — La rueda: dos trampas, y sólo una era de la rueda

`8ecd4e673`, `c33634b69`, `59cf47aff`. La pantalla se veía y no se podía usar.
`0x3C200000` era `create_unimplemented_device`.

El modelo entero (bloque, IRQ 23 y manejador de teclado) está en
`ipod_classic_wheel.c`, y su cabecera lleva cada línea del mapa de registros con
la instrucción de Apple que la establece. Lo que merece estar en la historia son
las tres decisiones.

**Primera trampa: `WHEELMASK` tiene que leerse honradamente.** El firmware lo
guarda, lo pone a cero alrededor de cada transacción sondeada y lo restaura.
Mientras el bloque era `create_unimplemented_device` toda lectura daba cero, así
que **lo que restauraba era cero**, y la interrupción quedaba enmascarada para
siempre. El log de arranque lo decía todo el rato: **doce escrituras de `0` a
`+0x10` contra una sola de `1`**. Contar accesos sin mirar los valores lo habría
escondido.

**La decisión de diseño que no es evidente: los paquetes se espacian con un
temporizador, no se encolan de golpe.** El decodificador de giro `0x0807a670`
necesita **dos paquetes seguidos con el dedo puesto** antes de calcular ningún
delta — el primero sólo arma —, y el ISR sobrescribe `0x089116d8+0x20` en cada
paquete. Si dos entran antes de que la tarea de rueda corra una vez, el paquete
de armado desaparece y el movimiento se lee como primer toque y se descarta.
**Una FIFO sola no basta**: hace falta que el huésped vea cada paquete como su
propia interrupción.

**Segunda trampa, y no era la rueda: el interruptor de bloqueo del PMU.** Con la
rueda puesta, el giro funcionaba y **los botones no**. Medido, con la tecla
mantenida tres segundos y leyendo el bloque de estado del propio firmware:
`0x089116d8+0x1C = 1` (el paquete llegó y el ISR lo decodificó) y `+0x14 = 1`
(la tarea corrió el detector de flancos y llamó al despachador). Y la pantalla,
quieta.

La causa está dentro del despachador de teclas, en `0x0803f3e0` — **la
instrucción número 21** de `0x0803f390`, después de las cuatro comparaciones que
remapean el código de tecla —, que acaba leyendo el registro `0x87` (`GPIOSTAT`)
del PMU por I²C y **vuelve inmediatamente si dice «bloqueado»**. Nuestro PMU lo
devolvía a cero, así que **cada botón se tiraba en silencio**. Y el giro **no
pasa por ese despachador**, por eso el fallo parecía de la rueda.

Rockbox dice qué es ese bit y llega a la misma polaridad desde el otro lado
(`pmu-6g.c:244`, `holdswitch = !(GPIOSTAT & GPIO2)`): **bit puesto = bloqueo
desactivado**. El arreglo es una línea en `ipod_classic_pmu_reset()`, marcada
como VALOR DE CONVENIENCIA, porque una máquina sin interruptor físico que mover
tiene que arrancar con él desactivado o no se puede usar.

> **Lección, y es el `HI_IRQ` del 5G otra vez**: que el modelo levante una
> interrupción no significa que el firmware la reciba, y **comprobar el camino
> entero significa comprobarlo para cada tipo de evento**, no una vez. El giro y
> el botón entran por el mismo bloque, comparten ISR, y a partir de la tarea de
> rueda se separan.

### Lo que se ve, medido y mirado

`sh tools/ipod_classic/navtest.sh`, tres carreras con cifras idénticas:

```
00-idle             75463 px encendidos
01-idle-again       75463 px             0 px distintos   <- el suelo de ruido
02-wheel-fwd        75432 px          8000 px distintos   (8000 en la lista, 0 en el panel)
03-wheel-fwd2       75463 px          8000 px distintos
04-wheel-back       75432 px          8000 px distintos
05-select           76800 px         50540 px distintos
06-select-later     76800 px             0 px distintos
07-menu             76800 px             0 px distintos
```

Los **8 000 píxeles** son exactamente lo que cuesta mover la barra: dos cajas de
160×25 encendiéndose y apagándose. El suelo de ruido de esta pantalla es **0**, y
`01-idle-again` lo comprueba **en cada carrera antes de creerse nada** — si deja
de ser 0, el test suspende, porque todo su veredicto descansa en ese cero.

Y mirado, que es la otra mitad: `e` lleva la barra de **English** a **日本語**, `q`
la devuelve, y Enter **cambia de pantalla** al icono de la nota musical con
**「ミュージックが…」**, es decir **en el idioma que estaba resaltado**. Esa
coincidencia es lo que convierte «cambiaron muchos píxeles» en «la interfaz hizo
lo que se le pidió».

Los contadores del modelo cuadran con las teclas una por una: **14 paquetes** =
1 respuesta sondeada del arranque + 3 giros × (armado + movimiento + dedo fuera)
+ Enter (pulsar y soltar) + `w` (pulsar y soltar); y **13 flancos** = 14 − 1,
porque el paquete del arranque se encola con `WHEELMASK` a cero.

### Y una corrección al brief del que salió el trabajo

**El huésped no sondea la rueda 120 veces por segundo.** Hace **una sola**
transacción sondeada en el arranque (con cinco reintentos) y a partir de ahí es
puramente por interrupción, IRQ 23. Las miles de lecturas de `+0x14` que se
contaban eran el giro de espera de esa única transacción contra un registro que
devolvía cero. La conclusión de entonces («ya hay quien escuche») era
**casualmente correcta**; el mecanismo, no — y un modelo que sólo devolviera
datos en `WHEELRX` sin levantar la línea no habría producido ni un solo evento.

---

## Tres veces un descarte resultó ser la pista buena

Esto vale más que la lista de registros, y por eso está en medio del documento y
no en un apéndice.

### Los tres casos

**1. `0x38900000`, descartado por «ningún registro recibe algo con pinta de
dirección».** Retirado en `70c29550a`, resuelto en `3e67e5bbc`: era el
compositor de vídeo, la pieza que dibuja. La prueba era cierta y la ausencia era
**consecuencia del bloqueo que se estaba investigando**: la dirección de origen
sólo se escribe cuando el volumen de recursos está montado, y encima viene de un
sitio distinto (`0x081276dc`) del que escribe la geometría de la capa. Se midió
el sistema **en el estado en que estaba roto** y se leyó el resultado como una
propiedad del hardware.

**2. La imagen `rsrc` del `.ipsw`, dada por vacía durante días.** Resultó ser un
**volumen FAT16 de 78 MB** con `Resources/Fonts` dentro, es decir con las
tipografías sin las cuales la interfaz no puede componer nada. Un vistazo a
`dev_offset` sin el sesgo de `0x1000` cae en el relleno y se lee como una imagen
vacía. Corregido en `03421c85d`, documentado en
[01-firmware-ipsw.md](01-firmware-ipsw.md).

**3. El ATA, señalado como culpable del cuelgue con disco, era inocente.** El
diagnóstico «el agujero está entre *el DMA terminó* y *despierta a quien lo
pidió*» apuntaba al hueco correcto y al periférico equivocado: el ATA cierra su
ciclo entero, la interrupción llega y el semáforo despierta. Quien no volvía era
un `memcpy` **posterior** por DMAC0. Corregido en `3e67e5bbc`, §14 de
[11](11-classic6g-retailos.md).

### Qué tienen en común

**(a) Los tres se apoyan en una prueba negativa, y una prueba negativa sólo
descarta si sabes que lo que buscas tendría que haber aparecido en el estado en
que mediste.** En los tres casos el sistema estaba roto justo aguas arriba de
donde se produce la evidencia: sin volumen montado no hay dirección de origen
que escribir, y el `memcpy` que cuelga ocurre *después* de que el ATA haya
terminado y se haya callado.

**(b) Los tres cierran una pregunta con una sola observación y sin caso de
control.** El proyecto ya tenía la regla escrita — es la lección 2 de
[09-historia-5g.md](09-historia-5g.md), *«un experimento negativo sólo descarta
la hipótesis si el experimento estaba bien construido»* — y aun así.

**(c) En los tres, la evidencia buena estaba en la misma medida que el
descarte.** Las 793 direcciones escritas en `0x38900000` incluían una tabla de
gamma identidad de 768 palabras y la geometría del panel al píxel, dos firmas
que sólo tiene un camino de vídeo. En la tabla del directorio del `.ipsw`, la
entrada `osos` de al lado se extraía perfectamente con el sesgo aplicado. Y en
la traza del ATA, el driver cierra limpiamente (`ATA_CFG = 0x640`) y **no vuelve
a hablar**, que es exactamente la firma de un periférico que ha terminado su
trabajo, no la de uno atascado.

**(d) El coste crece con lo tarde que se revisan.** El descarte del ATA costó
una sesión entera de `gdbpoke.py` bajando por la cadena de llamadas equivocada;
el de `rsrc`, días de suponer que las tipografías tenían que estar en algún
sitio raro; el de `0x38900000`, la única pieza que dibuja.

Y un cuarto rasgo que no es casualidad: **en dos de los tres, el sospechoso
verdadero ya estaba escrito en la lista de candidatos**. La lista de «por
descartar, en orden de coste» del §6 de [11](11-classic6g-retailos.md) tenía a
`0x38900000` de número 1 y a DMAC0 de número 4 — **el último** —, y DMAC0
resultó ser el más barato de arreglar de los cuatro, porque QEMU ya trae
`hw/dma/pl080.c`.

### Qué habría que haber hecho distinto

Cinco reglas, en orden de rendimiento:

1. **Un descarte se escribe con su condición de caducidad dentro.** No
   «`0x38900000` no es el origen del cuadro», sino «`0x38900000` no recibe
   direcciones **en un arranque sin disco**, que es el único que sabemos llevar
   hasta aquí». La frase con la condición dentro se re-evalúa sola en cuanto
   cambia el estado; la frase sin ella se queda en la wiki para siempre.

2. **Antes de creerse un cero, comprobar que el experimento produce un uno en un
   caso del que ya sabes la respuesta.** Esta regla existe en este proyecto y
   tiene nombre: es como se descubrió que `grep -c '^0xDIRECCION'` sobre
   `-d in_asm` sólo vale para entradas de bloque — se probó con `0x22003b1c`,
   que se ejecuta miles de veces, y salió **cero**. La regla nació ahí y **no se
   aplicó** a ninguno de los tres descartes.

3. **La evidencia positiva pesa más que la negativa, y hay que puntuarla
   aparte.** Una tabla de gamma identidad y la geometría del panel al píxel son
   *firmas*; «no veo una dirección» es una *ausencia*. Una firma que no tiene
   otra explicación gana a una ausencia que tiene varias. Corolario operativo, y
   es la regla que más ha rendido en este proyecto: **contar valores, no
   accesos**.

4. **Cuando el sospechoso obvio es también el último bloque que tocaste,
   comprobar el siguiente de la lista cuesta un minuto.** El ATA se acababa de
   modelar, se estaba trazando con `ATATRACE=1` y era el único que hablaba: era
   el sospechoso disponible, no el probable.

5. **Los descartes viven en una tabla, no en prosa.** Con fecha, con la prueba
   concreta y con qué haría falta para reabrirlos. Los tres estaban enterrados
   en párrafos, que es donde un descarte deja de revisarse. La tabla siguiente
   existe por eso.

---

## Catálogo de hipótesis retiradas

Índice rápido, para no volver a proponerlas.

| # | Afirmación | Estado | Dónde |
|---|---|---|---|
| 1 | El firmware del Classic es inabordable sin extraer la clave GID del silicio | **Falsa**: no hay que extraerla, basta con que descifre el propio aparato por USB | `b71d16f3d`, [10](10-classic6g-emcore.md) |
| 2 | Hay que escribir de cero ATA, click wheel y LCD del S5L8702 | Falsa: `freemyipod/qemu@s5l8702-dev` ya existía. (Se escribieron de cero igualmente, desde el desensamblado) | `3f5cd7280`, `e17924eab` |
| 3 | El mapa de bases de [02](02-s5l8702-hardware.md) | Colaron bases legacy de S5L8700/8701 (`SPI0`, `ADC`, `RTC`, `FMC`, `USB HC/FC`, `INT_BASE`, `MS_BASE`) | `e17924eab`, [08](08-s5l8702-fuentes-externas.md) |
| 4 | "Mikey" es el controlador del click wheel | Falsa: es el micrófono y mando del jack, I²C0 `0x72` | `e17924eab` |
| 5 | `rsrc` y `hash` están los dos rellenos de `0xFF` | **Falsa para `rsrc`**: es un FAT16 de 78 MB con las tipografías. Cierta para `hash` — y eso es lo que la hizo creíble | `03421c85d`, [01](01-firmware-ipsw.md) |
| 6 | El framebuffer va por DMA, porque llega una palabra de píxel por escritura de CPU | Falsa dos veces: eran **cero** píxeles y ocho bytes de parámetro de orden, y ningún canal de DMA apunta jamás a `LCD_WDATA` | `02c6f3327`, `4bf80dacd` |
| 7 | `0x38900000` descartado: "ningún registro recibe nada con pinta de dirección" | **Cierta, y no significaba eso.** Es el compositor de vídeo | `70c29550a` → `3e67e5bbc` |
| 8 | El bloqueo con disco está en el ATA | **Falsa**: el ATA cierra su ciclo entero; es un `memcpy` posterior por DMAC0 sin IRQ 16 | `3e67e5bbc` |
| 9 | «El parser `]ih[` nunca se ejecuta», medido en `0x08057160` | La conclusión aguantó; **la medida no valía**: no es entrada de bloque de traducción | `b9c2b38a7` |
| 10 | El pin 86 del GPIO es "toda la razón" por la que un arranque con disco no dibuja | Necesario, **no suficiente**: el anuncio se tiraba después en `0x080b2ce8` | `70c29550a` |
| 11 | Con disco el bloque de LCD se toca dos veces | Son **cinco** escrituras | `70c29550a` |
| 12 | Las tres imágenes de disco dan trazas de ATA idénticas de 682 líneas | 680 líneas y una diferencia, y la diferencia es ruido de carrera a carrera en la dirección del búfer | `70c29550a` |
| 13 | El huésped sondea la rueda ~120 veces/s, "así que ya hay quien escuche" | Conclusión casualmente correcta, **mecanismo falso**: una transacción sondeada y después IRQ 23 | `8ecd4e673` |
| 14 | Ninguna capa se solapa nunca, así que el orden Z no está medido | **Falsa**, y era justo el dato que lo sostenía: la capa 1 es un barrido con `x + w = 320` en todos los pasos | `03421c85d` |
| 15 | Los botones no llegan porque falla el modelo de la rueda | Falsa: era el interruptor de bloqueo del PMU, que el giro no atraviesa | `8ecd4e673` |
| 16 | `0x0803f3e0` es "la segunda instrucción" del despachador de teclas | Es la **vigesimoprimera**, y hay una segunda puerta detrás en `0x0803f3ec` | `59cf47aff` |
| 17 | Los ocho nombres de registro de la rueda son de Rockbox | **Seis de los ocho son invención de este puerto**: Rockbox sólo nombra `WHEELINT`, `WHEELRX` y `WHEELTX` | `59cf47aff` |
| 18 | La cabecera de `ipod_classic_disp.c`: "un compositor aquí estaría inventando píxeles" | Contradecía a `ipod_classic_disp_compose()`, treinta líneas más abajo | `03421c85d` |
| 19 | `navtest.sh` del Classic aprueba si los giros mueven el resaltado | Aprobaba **también** con el suelo de ruido roto, que es la premisa de todos sus umbrales. Ahora suspende | `59cf47aff` |

---

## Técnicas de depuración, por lo que rindieron

1. **Leer stderr.** El firmware imprime sus panics por semihosting, y estuvo
   diciendo `CPU_exception_handler` y `SWRCON` diecinueve veces cada veinte
   segundos mientras se miraban histogramas de `-d unimp`. Coste de encenderlo:
   una línea.
2. **`gdbpoke.py` / `gdbrun.sh`.** Responde «¿qué hay en `r0` cuando el huésped
   llega aquí?», que es la pregunta que `-d in_asm` no puede contestar. Toda la
   cadena de llamadas del §9 de [11](11-classic6g-retailos.md) y la corrección
   de las tipografías salen de ahí. Con `--skip` se salta una llamada **sin
   tocar la imagen de Apple**, que es como se demostró que esquivar el fallo no
   lleva a ninguna parte.
3. **`grep -c '^0xDIRECCION'` sobre un log de `-d in_asm`**, para «¿corrió
   esto?» en un segundo. **Sólo vale para entradas de bloque**: sin capstone,
   `-d in_asm` imprime una dirección por bloque de traducción. Desensambla la
   dirección y comprueba que hay un prólogo antes de creerte el cero.
4. **Trazas por bloque con el PC del huésped y totales al final**: `DISPTRACE`,
   `LCDTRACE`, `ATATRACE`, `WHEELTRACE`, `GPIOTRACE`. La regla que las hace
   útiles es imprimir **valores**, no sólo accesos — así se encontró la tabla de
   gamma —, y la que las hace fiables es que **los totales cuenten todo**: un
   contador que compara con el último valor visto en vez de con los ya vistos no
   cuenta, y hubo uno.
5. **`pmemsave` + `ppm2png.py --raw 320x240:argb8888`.** Mirar el framebuffer del
   huésped **antes** de tener camino a pantalla. Es lo que confirmó que la
   imagen estaba entera en DRAM y sólo faltaba copiarla.
6. **Casos de control.** `lcdtest.S` para el panel (76 800/76 800 y 1920
   colores, y la rampa **mirada**); `lr` cambiando entre paradas para el
   depurador; el suelo de ruido de 0 comprobado en cada carrera de `navtest.sh`.
   Un instrumento que no se prueba produce medidas que no valen, y aquí produjo
   dos.
7. **Leer el bloque de estado del propio huésped por el monitor.** `0x089116d8`
   es donde el ISR de la rueda deja lo que decodificó: `+0x0C` y `+0x10`
   moviéndose demuestran que **la interfaz drenó el evento**, que es más de lo
   que prueba una interrupción entregada.
8. **`findlit.py`**: decodificar los `LDR` relativos a PC que cargan un literal,
   en vez de buscar bytes. Es lo que hace que «hay exactamente tres sitios que
   tocan `0x3C100000`» sea una lista completa y no un muestreo.
9. **Diferencia de píxeles entre fotogramas, con umbrales.** Mover el resaltado
   un elemento son 8 000 píxeles; una pantalla nueva, 50 540. Unos pocos píxeles
   son el reloj de la barra de estado y no prueban nada — con la ventaja de que
   **esta pantalla no tiene reloj**, así que su suelo es exactamente 0.

---

## Cuestiones abiertas

Ninguna de estas está resuelta. Van aquí para que nadie las dé por cerradas.

**El hito que falta**

- **MENU no vuelve atrás** desde la pantalla que sigue a elegir idioma: 0
  píxeles, medido con esperas de 15 y 25 s. Y la tecla **llega entera**: con
  breakpoints a lo largo de `0x0803f390` se ve el código remapeado atravesar el
  despachador, llegar a `0x08170e94`, construir el mensaje `'kDwn'` y entrar en
  la cola del EventManager. Esa pantalla **no está sorda** — reacciona a un giro
  con 50 307 píxeles — y decide no hacer nada con MENU ni con un segundo Enter.
  Que sea porque el asistente de primer arranque no tiene a dónde volver es
  `INFERIDO`, no verificado.
- **El panel derecho no cambia al girar** (0 píxeles a la derecha en los tres
  giros), cuando la regla `delayedselected1 -> ShowSetting_Language` dice que
  debería. Apunta a la mitad temporizada de la interfaz (`TimerEventManager`,
  `0x081059c4`), y es el hilo más prometedor para las dos cosas.

**Cosas que el modelo elige y no deriva**

- **El orden de bytes de las capas** (`INFERIDO`, dos líneas de
  `ipod_classic_disp.c` si aparece evidencia mejor).
- **El ritmo de la rueda**: 40 ms es una constante de ajuste, no un dato del
  aparato. El ritmo real es `INDETERMINADO`.
- **`IICCON = 0x184` heredado del bootrom** (`INFERIDO`, y no habrá volcado de
  bootrom por USB).
- **El interruptor de bloqueo del PMU desactivado al arrancar**: VALOR DE
  CONVENIENCIA, marcado como tal.
- **Que `0x3C100000` sea un RNG**: `INFERIDO`. Su contrato de tres registros,
  verificado.

**Lo que no se ha podido comprobar**

- **La composición de varias capas nunca se ha visto en una imagen.** La capa 0
  sola ya contiene la pantalla entera; el barrido de la capa 1 dura menos de un
  segundo y ningún `screendump` lo ha cogido. Los 75 463 píxeles prueban muy
  bien la ranura 0 y **no prueban** el orden Z ni el recorte.
- **Si `LCD +0x70` engancha un cuadro o sólo habilita el barrido.** El modelo
  compone en `gfx_update`, que es una decisión de modelado dicha en voz alta en
  `ipod_classic_lcd_gfx_update()`.
- **Qué significan bit a bit `WHEELCON`, `WHEELPEND[1:2]`, el `0x3A980` de
  `WHEELDIV` y la orden `0x8000062A`**; y **por qué el hardware real acepta las
  dos codificaciones de `WHEELTX`** (la de Rockbox y la de Apple). Ninguno hace
  falta.
- **Qué es el pin 86 del GPIO en la placa.** Lo que hace en el firmware de
  Apple, verificado.

**Lo que directamente no está**

- **El arranque sin disco** sigue en su ciclo de caída, y ahora se sabe que es
  irreparable por ese camino: no hay ninguna tipografía dentro de la imagen.
- **Audio** (I²S, CS42L55), **USB**, **NAND/FMC**, **CE-ATA sobre SDCI**, el
  **DSP CalmADM2E**, el **RTC** y el **AES** del propio SoC: sin modelar.
- **Las capas de 4 y 8 bits** no se decodifican (falta la paleta) y ninguna se
  ha visto usada; **el escalador y la matriz de color** se guardan y no se
  honran, porque todos los arranques medidos escriben la identidad.
- **La bootrom**: no hay volcado y no lo habrá por USB.

---

## Nota de mantenimiento

En este puerto un comentario del propio código ha quedado mintiendo al menos
tres veces —`ipod_classic_disp.c` llegó a decir lo contrario de lo que hacía la
función que tenía treinta líneas debajo—, y la wiki ha ido por detrás del árbol
otras tantas. Si tocas algo de esto:

- Las cabeceras de `ipod_classic.c`, `ipod_classic_disp.c` y
  `ipod_classic_lcd.c` enumeran **lo que no está modelado**. Esa lista caduca
  con cada commit que modela algo.
- El mapa de [02](02-s5l8702-hardware.md) y la lista de hitos de
  [04](04-roadmap.md) son los dos sitios que más se quedan atrás.
- Las secciones de diagnóstico superadas **no se borran**: se marcan con un
  banner de *superado* y un enlace a lo que las sustituye. Ese es el valor de
  estos documentos, y es por lo que el §6 y el §9 de
  [11](11-classic6g-retailos.md) siguen ahí describiendo un estado que ya no
  existe.
