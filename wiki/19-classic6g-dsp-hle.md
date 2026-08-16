# HLE del DSP de audio del retailOS: especificación e inventario de incógnitas

Documento de entrega de **dos investigaciones de ingeniería inversa** (una de
desensamblado estático, otra de traza en ejecución) que perseguían el mismo
objetivo: **especificar exactamente qué tiene que hacer nuestro modelo del DSP
para que el retailOS de Apple reproduzca audio en el emulador**, de forma que el
siguiente workflow pueda implementar el HLE sin volver a investigar.

Es la continuación directa de [17-classic6g-dsp-retailos.md](17-classic6g-dsp-retailos.md)
(el plan de partida) y de [14-classic6g-audio.md](14-classic6g-audio.md) (el
camino de audio aguas abajo, ya modelado). **Corrige** la premisa de [17](17-classic6g-dsp-retailos.md)
con medida.

Direcciones VA del firmware = offset en `osos_decrypted.bin` + `0x07FF51B8`.
Marcas usadas en todo el documento:

- **VERIFICADO** — medido en ejecución o leído del desensamblado.
- **INFERIDO** — razonado desde algo verificado; se indica la fuerza.
- **DESCONOCIDO** — no se sabe, y no se finge. Se propone cómo averiguarlo.

---

## 0. Veredicto que cambia el marco (leer esto primero)

**La premisa de [17](17-classic6g-dsp-retailos.md) — "el retailOS referencia el
DSP CalmADM2E MUCHÍSIMO (245 y 213 literales) y decodifica el MP3 ahí" — NO se
sostiene.** Las dos investigaciones, por caminos independientes (desensamblado y
traza), llegaron a lo mismo:

1. **El retailOS no toca `0x39000000` (ADM_SFR) ni `0x3F000000` (ADM) en ningún
   estado alcanzable.** VERIFICADO por dos medidas separadas: 0 accesos MMIO en
   arranque en reposo, navegación completa y decenas de intentos de PLAY.
2. **Ese "recuento de referencias" era un artefacto.** El literal dominante
   `0x3F000000` es el **float `0.5f`** IEEE-754 (matemática de ganancia), y
   `0x39000000` aparece como la constante FourCC `0x39530000` y como datos
   (píxeles ARGB, tablas de bytes). **Ni una sola carga de puntero** a ninguna de
   las dos bases, ni un solo acceso `base+offset` al mapa de registros del
   CalmADM2E. VERIFICADO por desensamblado.
3. **El decodificador de audio del retailOS es el framework de Apple "MeCCA" con
   códecs cargables (`AudioCodecs`), decodificando en software** (cadenas
   `MeCCADecode`, `MeCCA_MediaPlayer`, `MeCCA_StreamCache`, `AudioCodecs`,
   `alac: bit depth = %d`, `MP3ExampleTask`, `ERROR: unknownCodec loaded !!!`).
   INFERIDO (fuerte).
4. **El verdadero muro no es "modelar el DSP", es que la reproducción es
   inalcanzable:** los botones no registran sobre la **lista de canciones**
   (`wiki/14 §5`), así que el camino de `play` **aborta antes** de tocar ningún
   periférico de decodificación. Sin `play` sostenido no hay protocolo que
   trazar. VERIFICADO.

**Consecuencia para el HLE:** que el mecanismo de audio del retailOS sea el
CalmADM2E vía las ventanas MMIO de Rockbox está **NO VERIFICADO — y la evidencia
disponible lo contradice.** Construir un HLE ahí a ciegas sería inútil. Este
documento entrega (a) el mapa de registros *tal como quedaría si la hipótesis
CalmADM2E se confirmara* (de Rockbox, sin uso comprobado), (b) lo que de verdad
está verificado, y (c) un **plan de implementación en árbol de decisión** cuyo
primer nodo es una medida barata que decide dónde va el HLE.

---

## 1. Reconciliación de las dos investigaciones

Las dos coinciden en lo esencial (§0). Difieren en un punto que hay que dejar
anotado con honestidad:

| Punto | Investigación de código (desensamblado) | Investigación de traza (runtime) |
|---|---|---|
| ¿Se alcanzó "Now Playing"? | **Sí** (por métrica de píxeles: `diff=28160` sobre lista `diff=38030`), y al pulsar PLAY capturó **18 accesos a `0x39300000`** (PCs `0x080a23e4`, `0x0814051c–58`), con un **bucle de polling** en pc~`0x08140540` | **No**: el SELECT/PLAY **no registra** sobre la lista; 30 rondas sin cambio de pantalla, 0 accesos a ningún bloque no modelado |
| ¿Accesos a `0x39/0x3F` (ADM)? | **0** | **0** |
| ¿Audio? | `play.wav` = **0 muestras no nulas** | silencio (no llegó a play) |

**Reconciliación (INFERIDO):** una investigación observó actividad en
`0x39300000` durante un intento de PLAY; la otra ni siquiera enganchó PLAY. Lo
compatible con ambas es que **los accesos a `0x39300000` pertenecen al compositor
de la pantalla Now Playing (escalado de la carátula), no al audio** — coinciden
con el escalador 2D `MeCCA` (ver §2b). Es decir: aunque una corrida llegara a
dibujar Now Playing, el **audio siguió siendo silencio y el ADM siguió sin
tocarse**. Ninguna de las dos observó un periférico de *decodificación de audio*.
El bloque `0x39300000` es, con evidencia fuerte, **vídeo/imagen**, no el DSP de
audio — es una **pista falsa**.

---

## 2. Pregunta 1 — El MAPA de registros

Hay que separar dos cosas que [17](17-classic6g-dsp-retailos.md) mezclaba: el
mapa *candidato* (CalmADM2E, de Rockbox, **sin uso comprobado en el retailOS**) y
el mapa *realmente conducido* (`0x391/2/3`, vídeo).

### 2a. Mapa candidato: CalmADM2E (de Rockbox `s5l87xx.h:64-137) — NO VERIFICADO EN USO

Reproducido para que el implementador lo tenga a mano **si** el nodo de decisión
del §6 confirma que el audio pasa por aquí. **Advertencia: no se ha medido ni un
solo acceso del retailOS a estos registros.** Dos vistas del mismo bloque.

**El buzón (mailbox) host ↔ DSP:**

| Registro | `ADM_BASE=0x3F000000 +` | `ADM_SFR_BASE=0x39000000 +` | Rol (según Rockbox) |
|---|---|---|---|
| `CONFIG0` / `CONFIG1` | `0x00` / `0x02` (16b) | `0x00` (`ADM_CONFIG`, 32b) | configuración/control |
| `COMMUN` | `0x04` (16b) | `0x04` (32b) | **control de comunicación** (el "timbre": el ARM da la orden aquí) |
| `DDATA0..7` | `0x06..0x14` (16b) | `0x10..0x2C` (32b) | **datos host → DSP** (8 palabras: argumentos del comando) |
| `UDATA0..7` | `0x16..0x24` (16b) | `0x30..0x4C` (32b) | **datos DSP → host** (8 palabras: respuesta) |

**Los punteros a buffers en DRAM** (el host le dice al DSP dónde están sus
regiones; el HLE los leería para saber de dónde leer el comprimido y dónde
escribir el PCM):

| Registro | `ADM_BASE +` (H/L, 16b c/u) | `ADM_SFR_BASE +` (32b) | Rol |
|---|---|---|---|
| `IBASE` | `0x26`/`0x28` | `0x50` | inicio del **firmware del DSP** (código CalmRISC). El HLE lo **ignora** — no ejecuta el Calm |
| `DBASE` | `0x2A`/`0x2C` | `0x54` | inicio del **área de datos** del CalmRISC |
| `XBASE` | `0x2E`/`0x30` | `0x58` | área X del MAC |
| `YBASE` | `0x32`/`0x34` | `0x5C` | área Y del MAC |
| `S0BASE` | `0x36`/`0x38` | `0x60` | inicio del **buffer secuencial 0** (INFERIDO: entrada o salida) |
| `S1BASE` | `0x3A`/`0x3C` | `0x64` | inicio del **buffer secuencial 1** (INFERIDO: el otro) |

**Estado/control de los buffers secuenciales (solo en la vista `ADM_BASE`, 16b):**

| Registro | `ADM_BASE +` | Rol |
|---|---|---|
| `CACHECON` / `CACHESTAT` | `0x3E` / `0x40` | control/estado de caché del Calm |
| `SBFCON` | `0x42` | **control** de los buffers secuenciales (arranca/para el modo anillo) |
| `SBFSTAT` | `0x44` | **estado** de los buffers secuenciales (candidato a "bloque listo") |
| `SBL0OFF` H/L | `0x46`/`0x48` | offset actual del anillo 0 (cuánto consumido/producido) |
| `SBL1OFF` H/L | `0x4A`/`0x4C` | offset actual del anillo 1 |
| `SBL0BEGIN` H/L | `0x4E`/`0x50` | inicio del anillo 0 |
| `SBL1BEGIN` H/L | `0x52`/`0x54` | inicio del anillo 1 |
| `SBL0END` H/L | `0x56`/`0x58` | fin del anillo 0 |
| `SBL1END` H/L | `0x5A`/`0x5C` | fin del anillo 1 |

Reloj del DSP: `DSPCLKMD` en `CLK_BASE+0x38` = `0x3C500038`. **VERIFICADO que el
retailOS NO lo carga como literal** (investigación de traza) — otra señal de que
el DSP no se arranca por esta vía.

**Qué es cada tipo, para el HLE (si se confirmara):** `COMMUN` = timbre/comando;
`DDATAn` = argumentos de entrada; `UDATAn` = resultados; `*BASE` = punteros a
regiones DRAM; `SBFCON/SBFSTAT`+`SBLxOFF/BEGIN/END` = los dos anillos entrada/
salida y su avance; `CONFIG0/1` = control global. **Cuál de S0/S1 es entrada y
cuál salida es DESCONOCIDO** (nunca se programaron).

### 2b. Mapa realmente conducido al reproducir: `0x39100000 / 0x39200000 / 0x39300000` — VERIFICADO, es vídeo/imagen (INFERIDO fuerte)

Esto es lo que la traza SÍ capturó. **Driver en `0x08140000–0x08141cc8`** (más
satélites `0x080543e0`, `0x08072810`, `0x080998a4`, `0x080a23dc`, `0x080af960`),
con **33 cargas `ldr rX,=0x391/2/3 0000`** y **0** de `0x39000000`.

Mapa de registros por bloque (de `admscan2.py`, 443 accesos), común a los tres:

| Offset | Rol (INFERIDO del uso) |
|---|---|
| `+0x00` | **control/estado**: bit0 = run/enable, bit1 = idle/done (VERIFICADO por el patrón parar→esperar) |
| `+0x04` / `+0x06` | cabecera de 16 bits, escrita **byte-swapped (big-endian)** |
| `+0x08` | word de cabecera (32b, big-endian) |
| `+0x3c` | modo (nibble bajo) |
| `+0x044..0x0d0`, `+0x080..0x158`, `+0x180..0x1c0`, `+0x200..0x26c` | bancos de config/coeficientes |
| `+0x280` = 1, `+0x284` bit0 | **enable** del bloque |
| `+0x3c0..0x3d4` | reset/clear |
| (bloque `0x39200000` llega hasta `+0x800`) | |

Secuencia verificada (desensamblado): **enable** (`0x080a23dc`:
`str #1,[base+0x280]; [base+0x284] |= 1`) → **cabecera byte-swapped** a
`+0x00/04/06/08` (`0x080a23fc`) → **run** (`+0x00` bit0) → **poll idle**
(`+0x00` bit1, hasta 10 veces con delay `bl 0x80c4e48`) → **teardown**
(`0x081404c4`: `+0x3c &= ~0xF; +0x00 &= ~1`).

**Por qué es vídeo/imagen y no audio (INFERIDO fuerte):** la función de setup
(`0x08140140`) carga `r7=0x39100000` y calcula **dos ratios de escalado
independientes por dimensión** — `r7+0x64 = (dstW<<12)/srcW >> 3` y
`r7+0x68 = (dstH<<12)/srcH >> 4` (división vía `bl 0x800f990`). **Dos** ratios 2D
= escalado de imagen (la carátula), no remuestreo de audio (1D). Concuerda con
las cadenas `MeCCAVideoDecode`, `MeCCA_VideoBufferMgr`, `MPEG4`. **Recomendación:
no construir el HLE de audio aquí.** (Confirmable: reproducir *vídeo* debería
activarlos; *audio sin carátula* no — ver §7.)

---

## 3. Pregunta 2 — La SECUENCIA que el retailOS espera

### Para el audio (CalmADM2E): **DESCONOCIDO.**

- No hay **ninguna** escritura a `IBASE/DBASE/XBASE/YBASE/S0BASE/S1BASE` ni a
  `CONFIG0/COMMUN/DDATAn` medida en ningún estado alcanzable (0 accesos,
  VERIFICADO). Por tanto **no se conoce** ni la carga de firmware, ni el arranque,
  ni el lazo "dame un bloque → aquí tienes PCM", ni valores reales.
- **Causa del vacío (VERIFICADO):** el camino de `play` aborta aguas arriba (muro
  de la lista, §0.4). El instrumento está validado (dispara con un `xp` de
  control), así que el 0 es un **negativo real**, no una sonda rota.

### Lo que SÍ está verificado (bloques de vídeo `0x391/2/3`, para referencia):

enable (`+0x280`/`+0x284`) → cabecera big-endian (`+0x00/04/06/08`) → run
(`+0x00` bit0) → **poll de `+0x00` bit1** hasta done → teardown (`+0x3c`, `+0x00`).
Es **polling**, no interrupción. Útil como plantilla de estilo del firmware
(cabeceras big-endian, control por bit0/bit1), pero **no es el lazo de audio**.

---

## 4. Pregunta 3 — El FORMATO exacto (entrada y salida)

- **Entrada del DSP (qué decodificar, de qué buffer, tamaño): DESCONOCIDO.** No
  se programó ningún `S0BASE/S1BASE`, así que no hay dirección ni tamaño de buffer
  de entrada que volcar. Se ignora incluso si al DSP se le pasarían frames MP3
  crudos o algo pre-parseado por el ARM. INFERIDO (débil) del framework: los
  códecs son cargables (`AudioCodecs`, ALAC/MP3), lo que sugiere entrada = stream
  comprimido del contenedor.
- **Salida (PCM): parcialmente VERIFICADO aguas abajo.** El formato que consume el
  I²S ya está modelado y probado con Rockbox: **PCM 16-bit estéreo entrelazado a
  44100 Hz** (48000 según pista; el retailOS fija `I2SCLKDIV`), leído por el DMA
  `IIS0_TX` → FIFO del I²S en `0x3CA00010` (ver [14](14-classic6g-audio.md)). **A
  qué buffer de DRAM escribiría el PCM el DSP: DESCONOCIDO** (sería el otro anillo
  S0/S1, no programado).
- **Endianness:** los bloques de vídeo `0x391/2/3` esperan cabeceras **big-endian**
  (VERIFICADO por el byte-swap en `0x080a23fc`); si el ADM siguiera el mismo estilo
  de firmware, sus cabeceras también lo serían (INFERIDO débil, no aplicable hasta
  confirmar la vía).
- **Medida de control:** todo el PCM capturable hoy es **silencio** (`play.wav` =
  0 muestras no nulas; el "empuje de 6144 muestras a 0" del arranque es el
  **silencio de init** del I²S, no un buffer de decodificación).

---

## 5. Pregunta 4 — La INTERRUPCIÓN o señal que cierra el ciclo

- **Para el audio (CalmADM2E): DESCONOCIDO.** No se observó ninguna IRQ ni señal
  de "bloque decodificado", porque el lazo nunca arrancó. El candidato natural en
  el mapa candidato sería `SBFSTAT`/`UDATAn` + una línea de IRQ del ADM, pero **no
  hay evidencia** de cuál ni de cómo se arma.
- **Lo observado en los bloques de vídeo es POLLING, no IRQ** (`+0x00` bit1). Es
  posible (INFERIDO débil) que el audio también use polling de un bit de estado en
  vez de interrupción; habría que confirmarlo cuando se alcance `play`.
- **Contexto de IRQ ya modelado aguas abajo:** el cierre del ciclo de *reproducción*
  (pedir el siguiente bloque) hoy lo da el **DMA de audio** (DMAC0, línea
  `IIS0_TX`, `IRQ_DMAC0 = 16`, con pacing `AUDIOPACE`), que **ya funciona** con
  Rockbox. Si el decode fuera en ARM (MeCCA sw), esa es la única IRQ que cierra el
  ciclo y **no hace falta modelar ninguna IRQ de DSP**.

---

## 6. Pregunta 5 — El PLAN de implementación HLE (árbol de decisión)

**No empezar escribiendo un `MemoryRegion` en `0x39000000`.** La evidencia dice
que sería trabajo muerto. El plan tiene un **prerrequisito** y un **nodo de
decisión barato** antes de comprometer arquitectura.

### Paso 0 (PRERREQUISITO, bloqueante) — Romper el muro de la lista de canciones

Sin `play` sostenido no hay protocolo que trazar; las dos investigaciones se
estrellaron aquí. El síntoma (VERIFICADO): la rueda mueve el resaltado en la lista
`Música → Canciones`, pero **SELECT/PLAY no registran** *sobre la lista* (sí
registran en los menús anteriores). Vía de ataque: comparar cómo el firmware
consume los paquetes de botón en un **menú** (funciona) vs. en el **widget de
lista** (no) — misma rueda, distinto consumidor. Es un muro de UI aguas arriba del
audio (ver [14 §5](14-classic6g-audio.md), [15](15-muros-classic6g.md)).

### Paso 1 (NODO DE DECISIÓN, barato) — Trazar quién llena el buffer PCM

En cuanto haya `play`, re-aplicar la instrumentación **DSPTRACE** (ya escrita y
validada; ver §8) **más** un breakpoint/watch sobre la fuente del DMA de audio:
el DMA `IIS0_TX` lee de una dirección DRAM → **poner un watchpoint de escritura en
esa dirección** y ver **quién la escribe**:

- **Rama A — la escribe código ARM** (PC en el rango del framework MeCCA, p.ej.
  cerca de `MeCCADecode`/`AudioCodecs`, o el driver PCM en IRAM `0x22008xxx`):
  entonces **el retailOS decodifica en el ARM** y *no hay DSP que emular*. El audio
  ya fluiría en cuanto el decodificador ARM corra sobre PCM real; el HLE se reduce
  a asegurar que el códec software no dependa de un periférico ausente. **Este es
  el resultado más probable** (INFERIDO fuerte, por ALAC/MP3ExampleTask/MeCCA sw).
- **Rama B — aparecen escrituras del retailOS a `0x39000000`/`0x3F000000`**:
  entonces la hipótesis CalmADM2E se confirma y se implementa el HLE del §6-detalle.
- **Rama C — la llena otro periférico** (ni ARM ni ADM): ahí está el "DSP" real; su
  base saldrá en el `io_floor` con el PC del huésped. Se modela ese, no el ADM.

**Coste:** una sola corrida con `play`. Decide toda la arquitectura. **No escribir
código de modelo antes de este paso.**

### Paso 6-detalle — El HLE del CalmADM2E, SOLO si el Paso 1 cae en la Rama B

Plantilla: `hw/arm/ipod_video/ipod_video_vc.c` (HLE del VideoCore del 5G) y
`hw/arm/ipod_classic/ipod_classic_disp.c` (estilo de dispositivo del Classic).

1. **`MemoryRegion`** de `0x39000000` (SFR, 32b) y `0x3F000000` (ADM, 16b) —dos
   vistas del mismo estado interno— registradas con prioridad **por encima** de
   los `create_unimplemented_device` actuales (`ipod_classic.c:343` y `:358`), como
   ya hace la traza. Handlers `read`/`write` que mantengan un `struct` con
   `CONFIG`, `COMMUN`, `DDATA[8]`, `UDATA[8]`, `IBASE..S1BASE`, `SBFCON/SBFSTAT`,
   `SBLx*`.
2. **Handler de `COMMUN` (write)** = el disparador. Cuando el ARM escriba el valor
   de "decodifica" (a determinar por traza en la Rama B), el modelo:
   a. lee `S?BASE` (el de entrada) + `SBLxOFF/BEGIN/END` para localizar el frame
      comprimido en DRAM (`cpu_physical_memory_read`);
   b. **decodifica en el host** con **libmad** (MP3) o **ffmpeg/libavcodec** (MP3+
      AAC/ALAC), enganchado aquí — es el punto de inserción del decodificador;
   c. escribe el PCM resultante al `S?BASE` de salida
      (`cpu_physical_memory_write`), avanza `SBLxOFF`, marca `SBFSTAT`/`UDATAn`;
   d. **avisa** al ARM: poner el bit de estado que espera y, si la Rama B mostró
      IRQ, `qemu_irq_raise` de la línea del ADM (número a determinar).
3. **Convivencia con I²S/DMA (ya modelado, no tocar):** el HLE solo produce PCM en
   DRAM. Desde ahí, el camino existente lo lleva solo: DMAC0/`IIS0_TX` con
   `AUDIOPACE` → FIFO I²S `0x3CA00000` → CS42L55 → altavoces/WAV. **No se toca**
   `ipod_classic_i2s.c` ni `pl080.c` ni el códec.
4. **Firmware del DSP:** el HLE **ignora** `IBASE` y el código CalmRISC. Esa es la
   ventaja del HLE: no hay que tener ni ejecutar el firmware del Calm.

### Riesgos y dependencias

- **Riesgo mayor (probable):** que la Rama B **no ocurra** y todo el §6-detalle sea
  inaplicable. Por eso el Paso 1 va primero. VERIFICADO que hoy la evidencia
  apunta a Rama A.
- **Dependencia de build:** libmad/ffmpeg en el árbol QEMU (flags de `meson`,
  licencia). Solo relevante en Rama A o B.
- **Riesgo de UI:** el muro de la lista (Paso 0) puede ser no trivial; sin él, nada
  de lo demás es medible.
- **Riesgo de batería:** el reset "Connect to Power" a ~46 s (`wiki/15`) puede
  cortar la captura; entrar en `play` antes o mitigar el modelo de batería.
- **No romper:** la instrumentación debe ser temporal (prioridad por encima,
  lecturas que devuelven 0 igual que el `unimp` actual) para no regresionar Rockbox
  ni `navtest` 6G/5G. Ambas investigaciones dejaron el árbol byte-idéntico al
  baseline; repetir esa disciplina.

---

## 7. Lo que sigue DESCONOCIDO y cómo averiguarlo

| Incógnita | Cómo cerrarla |
|---|---|
| **El mecanismo de audio real (ARM sw vs ADM vs otro periférico)** | Paso 1 del §6: watchpoint sobre la fuente DRAM del DMA `IIS0_TX`, ver el PC que la escribe. Una corrida con `play`. **Es la pregunta madre.** |
| **El muro de la lista de canciones** (bloquea todo) | Paso 0 del §6: comparar el consumo de paquetes de botón menú vs. widget de lista |
| **Si `0x391/2/3` son vídeo o (menos probable) audio** | Reproducir **vídeo** (deberían activarse) vs. **audio sin carátula** (no deberían). Cierra la clasificación |
| **Comando de decode, buffers E/S, IRQ, formato de entrada** (solo si Rama B) | Traza de `COMMUN`/`DDATAn`/`S?BASE` con la instrumentación DSPTRACE ya lista, en cuanto haya `play` |
| **Fuentes externas del protocolo ADM** | Revisar si **freemyipod/qemu** (rama `s5l8702-dev`) modela el ADM, y si **giek** documenta MeCCA/AudioCodecs. (Nota: `giek/architecture_MEMORY_MAP.md:31` etiqueta `0x39000000` como "GPIO" —baja confianza, se contradice con el modelo en funcionamiento— pero refuerza que "estas ventanas = decodificador" **no está probado**.) |

---

## 8. Tabla de confianza consolidada

| Afirmación | Marca | Evidencia |
|---|---|---|
| `0x39000000`/`0x3F000000` nunca se cargan como puntero ni se acceden por MMIO | **VERIFICADO** | `ldrscan.py`/`osana.py lit` = 0 LDR; 0 accesos en boot + navegación + 30 rondas de PLAY (dos investigaciones); control `xp` prueba que el instrumento dispara |
| `0x3F000000` en código = float `0.5f` (matemática de ganancia) | **VERIFICADO** | cada `mov #0x3f000000` → `bl 0x8374b18` (soft-float); vecino `0x3f800000`=1.0f |
| `0x39000000` en código = constante `0x39530000` (FourCC) + datos ARGB | **VERIFICADO** | `0x0810626c`: `orr`+`orr`; barrido de palabras = datos |
| HW realmente conducido al reproducir = `0x39100000/200000/300000` | **VERIFICADO** | `io_floor` en runtime (PCs `0x080a23e4`, `0x0814051c–58`); 33 `ldr`-literales |
| Esos bloques `0x391/2/3` son escaladores de imagen (vídeo/carátula), no audio | **INFERIDO** (fuerte) | dos ratios `(dst<<12)/src` por dimensión; cadenas `MeCCAVideo`/`MPEG4` |
| Secuencia de los bloques de vídeo: enable→cabecera BE→run→poll idle→teardown | **VERIFICADO** | desensamblado `0x080a23dc`, `0x080a23fc`, `0x081404c4` |
| Audio del retailOS = silencio en todo lo alcanzable | **VERIFICADO** | `play.wav` 0 no nulos; sin periférico de audio en el `io_floor` |
| El decode de audio es MeCCA/AudioCodecs en software (probablemente ARM) | **INFERIDO** (fuerte) | cadenas ALAC/`MP3ExampleTask`/`MeCCADecode`/`AudioCodecs`; ausencia de HW de audio conducido |
| La reproducción es inalcanzable por el muro de la lista (SELECT no registra) | **VERIFICADO** | 30 rondas sin cambio de pantalla; reproduce `wiki/14 §5` |
| Mapa de registros del CalmADM2E (§2a) | **VERIFICADO como def. de Rockbox / NO VERIFICADO en uso** | `s5l87xx.h:64-137`; 0 accesos medidos del retailOS |
| Secuencia de decode de audio, buffers E/S, comando, IRQ, formato de entrada | **DESCONOCIDO** | bloqueado por no alcanzar `play` sostenido |
| Salida PCM aguas abajo = 16-bit estéreo entrelazado 44100 Hz por I²S | **VERIFICADO** | modelado y probado con Rockbox ([14](14-classic6g-audio.md)) |

---

## 9. Resumen de la especificación (para el implementador con prisa)

1. **No** modeles `0x39000000`/`0x3F000000` todavía: no hay evidencia de que el
   retailOS los use para audio, y sí evidencia de que no.
2. **Primero** rompe el muro de la lista de canciones (§6 Paso 0) para conseguir
   `play`.
3. **Luego**, una corrida barata (§6 Paso 1): watchpoint sobre la fuente DRAM del
   DMA `IIS0_TX` → mira el PC que escribe el PCM. Eso decide todo:
   - PC en ARM/MeCCA → **decode en ARM, no hay DSP que emular** (más probable);
   - escrituras a `0x39/0x3F` → implementa el HLE del CalmADM2E (§6-detalle, mapa
     §2a);
   - otro periférico → modela ese.
4. Sea cual sea el productor de PCM, **aguas abajo ya está todo hecho** (I²S+DMA+
   CS42L55+captura). El HLE solo tiene que dejar PCM real en DRAM.

---

## 10. Referencias y artefactos

- [17-classic6g-dsp-retailos.md](17-classic6g-dsp-retailos.md) — el plan de
  partida (cuya premisa este documento corrige con medida).
- [14-classic6g-audio.md](14-classic6g-audio.md) — el camino de audio aguas abajo
  (I²S/DMA/CS42L55), ya modelado, y la medida del silencio.
- [15-muros-classic6g.md](15-muros-classic6g.md) — los muros del Classic, incluido
  el de la lista y el reset de batería; y la doctrina de validar el instrumento.
- [16-classic6g-rockbox.md](16-classic6g-rockbox.md) — el camino de audio que **sí**
  suena (Rockbox decodifica en el ARM).
- `assets/rockbox/firmware/export/s5l87xx.h:64-137` — definición del mapa CalmADM2E
  (fuente del §2a; sin uso comprobado en el retailOS).
- `hw/arm/ipod_video/ipod_video_vc.c` — **el precedente de HLE** (VideoCore del
  BCM2722): modelar el protocolo, no la ISA. Patrón a repetir si se confirma la
  Rama B.
- Instrumentación DSPTRACE (temporal, revertida en ambas investigaciones; lista
  para re-aplicar): patrón de `DISPTRACE` en `ipod_classic_disp.c`, mapeada por
  encima de los `create_unimplemented_device` de `ipod_classic.c:343`/`:358`.
- Herramientas de RE usadas: `ldrscan.py`/`osana.py lit` (cargas de literales,
  correctas donde `findlit.py` falla), `admscan.py`/`admscan2.py` (taint por base),
  `dumpctx.py`; desensamblado base VA `--adjust-vma=0x07FF51B8`.

---

## 11. El arranque del audio (continuación): el reproductor SÍ arranca; tres muros de RELOJ caídos; el decode sigue mudo

Con los arreglos del PMU (SELECT fiable + sin reset de batería, [15](15-muros-classic6g.md))
y el tracer no invasivo `pctrace`, se atacó el "Paso 0" del §6. Todo lo de abajo
está **VERIFICADO** por traza de PC (sin parar la CPU) o lectura de memoria en
runtime. VA = offset en `osos_decrypted.bin` + `0x07FF51B8`.

### 11.1 El reproductor SÍ arranca — refuta el encuadre "el handler no se despacha ni falla"

SELECT sobre **Shuffle Songs** (menú principal):
- `HandleShuffleSongsSelected` (0x081fa9e4) **se despacha**.
- Pasa su puerta: singleton `*0x089117fc` → `+0x3f4` = 4 (nº de canciones; `HandleMusicSelected`
  lee el mismo campo), 4≠0, no aborta.
- Corre el arranque completo: `ShufLAUNCH` (0x081faa34) → `setup20` (0x0826b858) →
  `StartPlayback` (0x08150d84) → `playA` (0x08024798), que **encola las 4 pistas**
  (itera el accesor de playlist `0x0802fe08`).
- `ShufLAUNCH` **completa**: vuelve de setup20, invoca `GotoNowPlaying` (0x081faa90),
  el handler **retorna** (0x081faaa4). El controlador Now Playing corre (región
  0x081a0000–0x081b2000, 618 bloques distintos).

Toda la maquinaria **síncrona** del arranque corre. El muro está aguas abajo, en el
pipeline MeCCA **asíncrono** — no en el despacho del handler.

### 11.2 Tres muros de RELOJ de audio (polls de bits de hardware que el modelo no ponía)

**Muro A — I2S CLKCON bit 1 (medido: 1.6e8 lecturas).** El driver de audio del
retailOS en IRAM (0x22008808–0x2200881c) **deshabilita** el reloj I2S
(`str 0,[CLKCON]`) para reconfigurarlo y luego **gira** `ldr CLKCON; tst #2; beq`
esperando el bit 1 (estado "reloj parado"). El modelo devolvía el 0 escrito → spin
infinito. **Arreglo** (`hw/arm/ipod_classic/ipod_classic_i2s.c`, read de CLKCON):
bit 1 = "reloj parado", puesto cuando el bit 0 (enable) está a 0.

**Muro B — SM1 (0x38500000) registro de identidad.** El init del reloj de audio
guarda 0x38500000 en su tabla de handles (setter `0x08083df0`, tabla @0x08ade488) y
luego lee **SM1+0xc80** (getter `0x08075d64`), lo compara con el magic `0x004e7309`
(literal `*0x080821cc`) y hace **`b .`** (cuelgue eterno en 0x080819b8) si no
coincide. SM1 estaba sin implementar (0) → cuelgue. VERIFICADO en runtime: en el
cuelgue SM1+0xc80 leía 0. **Arreglo** (`hw/arm/ipod_classic/ipod_classic.c`): SM1
pasa de `create_unimplemented_device` a un device mínimo que devuelve `0x004e7309`
en +0xc80.

**Muro C — SM1 bit "reloj listo".** Tras la identidad, el init **pollea SM1+0xa98**
esperando el bit 2 (`tst r0,#4; beq` en 0x08081af8) — el flag "reloj corriendo/lock".
SM1=0 → spin en 0x080a7658. **Arreglo**: SM1+0xa98 devuelve el bit 2.

**SM1 (0x38500000)**, etiquetado "clock divider" en el mapa, es en realidad el
**controlador de reloj de audio**: el retailOS hace un handshake de varios registros
(identidad +0xc80, estado +0xa98, escrituras a +0x824/+0xc48). El device modela SÓLO
el handshake que el arranque necesita, no el controlador completo (anotado en el
código).

### 11.3 Estado actual: la SALIDA se arma, el DECODE sigue mudo

Con los tres arreglos **+ `AUDIOPACE=1`** (necesario: el pacing del DMA es lo que hace
avanzar el pipeline; sin él la tarea de audio se duerme antes del init del reloj), la
**salida** se inicializa por completo (medido en runtime): I2S `CLKCON=0x1`,
`TXCON=0x0b100019`, **`TXCOM=0x6` (bit de run ACTIVO)**, reloj de audio armado.

PERO el **decodificador software MeCCA sigue sin producir PCM**: el mem-watch no
invasivo sobre los buffers ping-pong (0x22008c5c/0x22009c5c) mide **0 escrituras**
tras el SELECT; el I2S corre pero drena SILENCIO (buffer a cero); AUDIOWAV sale a
cero; el sistema entra en WFI idle; Now Playing no se dibuja. La tarea de decode está
**BLOQUEADA** esperando un evento (candidatos: la entrada comprimida desde disco vía
StreamCache que no completa; o un semáforo "buffer listo" que no se postea).

Resumen: **cayeron los muros de la SALIDA (reloj I2S/SM1); el muro que queda es el del
DECODE** — el núcleo de la Rama A. El decode MeCCA no arranca a producir PCM aunque la
salida ya esté lista para consumirlo.

### 11.4 Herramienta: mem-watch no invasivo en `pctrace`

`contrib/plugins/pctrace.c` extendido con `memw=0xLO-0xHI` (+`memcap=N`): registra cada
STORE de la CPU en el rango con su PC, sin parar la CPU (callback de memoria en el hilo
de la vCPU) — el watchpoint de la Rama A: en cuanto el decode escriba PCM en 0x22008c5c
saldrá el PC del decodificador. **Coste**: registra callback en cada instrucción →
ralentiza mucho; NO usarlo durante el boot completo (el huésped pierde deadlines y el
modelo de batería resetea). Usar ventana corta o ya en reproducción.

### 11.5 Lo que sigue

- **El decode MeCCA bloqueado** es el muro restante. Averiguar el punto de bloqueo
  RTXC. `pctrace` de QEMU 8.2 no lee registros, así que el LR del bloqueo necesita otra
  vía: stack de la tarea RTXC, o un plugin con la API de lectura de registros de QEMU
  9.x. Candidatos: lectura del MP3 (ATA/StreamCache) que no completa; semáforo no
  posteado.
- **SM1 (0x38500000)**: modelar el controlador de reloj de audio como es debido, no
  sólo el handshake mínimo; es probable que el decode dependa del reloj REAL, no de los
  bits "listo" fingidos.
- **AUDIOPACE**: el arranque del audio del retailOS lo necesita. Evaluar activarlo por
  defecto (seguro para Rockbox según [16](16-classic6g-rockbox.md)) o auto-activarlo al
  arrancar el I2S del retailOS.
- Sin regresión: `navtest` 6G pasa (05-select 19498 px, rueda dos mitades; 09-into-music
  es la flakiness conocida del SELECT, no regresión; 0 resets) y Rockbox sigue navegando.
  Los arreglos de CLKCON/SM1 sólo se ejercen en el camino de audio del retailOS.

### 11.6 SM1 modelado como dispositivo con estado (sustituye los bits fingidos)

Los tres arreglos de reloj de §11.2 se sustituyeron por un **dispositivo QEMU con
estado**, `hw/arm/ipod_classic/ipod_classic_sm1.c` (+ `.h`), registrado en la
máquina en vez del `create_unimplemented_device`. Modela el handshake COHERENTE
(el estado deriva de las escrituras del huésped, no constantes a mano):
- **+0xc80 (ID)** devuelve el magic `0x004e7309`.
- **sub-bloques** (`base + {idx*0x20; 0x100/0x180/0x200/0x230}`): al escribir un
  comando en `sub+0x10`, `sub+0x14 & 7` pasa a leer **7** ("alcanzó estado"), que
  es lo que el poll de 10 intentos en `0x080ba768` espera.
- **+0xa98 bit2 (clock-ready)** y **+0x400 (run latch)** se derivan de un flag
  `powered` que se pone con la primera escritura de bring-up (comando de canal,
  `+0x824`, `+0x400`, `+0xc48`).
- **+0xa48 (posición de reproducción)** se deriva de `i2s->tx_samples/2`.
- Todo lo demás es latch (se guarda lo escrito, lee 0 al reset). `SM1TRACE=1`
  imprime cada acceso con PC.

**MEDIDO (traza `SM1TRACE`, disco `~/6g/Emulti.img`, `AUDIOPACE=1`):** en el play
de Shuffle Songs el handshake corre **UNA vez, 33 accesos, y COMPLETA sin spin**:
`rd +0xc80 -> 0x004e7309`; `wr +0x110 = 4`; `rd +0x114 -> 7`; `wr +0x110 = 16`;
`wr +0x110 = 96`; config de códec `+0x93x..+0x97x` (una tabla desde `0x080b68b4`);
al final `rd +0xa48 -> 0x002a9000` (posición avanzando). **Conclusión medida: el
modelo SM1 está bien y NO es el muro** — el handshake que fingíamos ahora se
satisface de verdad, y aun así no suena. (Confirma el aviso del auditor por la vía
opuesta: modelar SM1 "de verdad" era correcto pero insuficiente.)

### 11.7 La premisa "0 escrituras al ping-pong" era un artefacto; el muro es el DECODE

Re-medido con `pctrace memw`: el buffer se escribe **millones** de veces (driver
IRAM `0x22000294/0x22000298` a ritmo de audio, casi todo silencio) y el **mixer SW
`0x080fb2b0`** mete exactamente los **clics de UI** (`AUDIOWAV`: ~9 360 tramas no
nulas, pico 11444, L==R, ~15 ráfagas aisladas — clics, NO música). El **tubo de
salida funciona de punta a punta**; lo que falta es que la MÚSICA decodificada
entre en él. (Coincide con el segundo informe de la sesión previa.)

### 11.8 El camino consumidor, mapeado entero (traza + gdb) — el copy SALTA por falta de PCM

Con `pctrace range=` disparado en `StartPlayback` (0x08150d84, `hits=1`) se capturó
la huella del arranque del decode: **~10 787 bloques distintos en ~367 ms**, luego
**cero bloques nuevos** — el sistema se asienta en un lazo estable (vuelve al event
loop de UI `0x08105a1c`/`0x0812xxxx`). Regiones tocadas: media player `0x0815`,
StreamCache `0x081f`, códec ALAC `0x0808`, tabla de códec `0x0832` (75 bloques).

La **cadena consumidora** (reconstruida con `pctrace pc=` y **gdbpoke** — pila +
registros en el copy, sin corromper la entrada porque el copy corre DESPUÉS de que
el SELECT despachó):

```
  tarea de audio 0x07ffb2d0 (entry de tarea RTXC; sin bl directo)
    -> veneer 0x07ff89f8 -> motor de audio 0x0808170c   (bring-up SM1 + switch de códec)
      -> mixer 0x0809a74c   (itera lista de streams: ldr r4,[r4,#4]; llama al render por stream)
        -> render 0x08016f14 -> 0x08016f68
          -> COPY PCM 0x08016e44   (llama al getter de posición SM1+0xa48 en 0x08091710)
```

El **copy 0x08016e44** corre **UNA vez** (`copyFn hits=1`) y toma la rama **SKIP**
(`copySKIP 0x08016ecc hits=1`, `copy32 0x08016eb4 hits=0`). La puerta: `ldrb
r1,[r7,#5]; cmp r1,#1; bne skip` en `0x08016e88`. **gdbpoke midió en runtime**
(estable entre corridas): `[r7+5] = 0` (la puerta exige 1), `[r4] = 0` (ruta de 32
bits), fuente `r0 = 0x08889098`, descriptores `r4=0x0bb12d54`, `r7=0x0bb12eb4`. El
volcado de la **fuente 0x08889098 NO es PCM** (punteros a sí misma + relleno
`0x5da35da3`); los descriptores están **medio-inicializados** (relleno `0x5da3`,
que en toda la imagen aparece 2 veces → valor de heap, no PCM). El copy **hace bien
en saltar**: no hay muestras válidas que copiar.

### 11.9 H1 CONFIRMADO por medida: el decodificador NO escribe ni una muestra

`pctrace memw=0x08888000-0x0888c000` (la región de la fuente 0x08889098) durante el
play: **8 192 stores, de sólo 2 PCs — `0x22000198` y `0x220001a0`, que son el
memset/clear de IRAM. CERO escrituras del decodificador.** Es decir, el buffer de
stream se **limpia** pero **nunca se rellena con PCM**. Esto zanja H1 vs H2:

> **El decodificador MeCCA nunca produce PCM (H1). No es un problema de ruteo/mute
> (H2): no hay muestras que rutear.** El motor `0x0808170c` corre una vez (por eso
> el handshake SM1 es único), mezcla el stream vacío, el copy salta, y la tarea se
> bloquea. La tarea PRODUCTORA (decodificador), aguas arriba del stream, está
> parada antes de la trama 0.

Datos colaterales: `media_dispatch_chain 0x081f87f8` corre 248 veces y `strcmp`
131 240 veces en la corrida (framework MeCCA muy activo por búsquedas con clave de
cadena). El códec (región ALAC `0x0808`, tabla `0x0832`) **sí** hace setup, así que
**no** es "códec no encontrado" tipo el muro de las fuentes (§2): el códec se
inicializa pero se bloquea antes de decodificar.

Mecanismo RTXC relevante (ya documentado en `ipod_classic.c:634-641`): las tareas
hacen `KS_wait` SIN timeout sobre el semáforo de un canal DMAC0 y sólo las despierta
el ISR de terminal-count (IRQ 16). Ese mecanismo **funciona** (el almacenamiento
monta, la salida drena). El pacing (AUDIOPACE) sólo afecta la línea IIS0_TX; el DMA
mem-a-mem del decodificador NO es paced. Así que el bloqueo del decode **no** es un
IRQ/pacing que falte — es un semáforo/evento de software aguas arriba.

### 11.10 El obstáculo exacto que queda y cómo cerrarlo

El decodificador MeCCA (tarea RTXC separada, aguas arriba del stream) se bloquea en
un `KS_wait` antes de la trama 0 y nunca rellena `[r5+12]` (la fuente del mixer). Su
PC/LR de bloqueo sigue **DESCONOCIDO**: la cadena de mixer/render/copy está mapeada
entera (consumidor), pero la tarea PRODUCTORA no se localizó por estático (despacho
por vtable + entradas de tarea sin `bl` directo). Para cerrarlo:
- **gdb ya funciona en el play** (no corrompe la entrada si el breakpoint cae tras
  el despacho del SELECT): poner un breakpoint en el primitivo `KS_wait` de RTXC y,
  cuando la tarea de audio se bloquee durante el setup, leer `bt`/LR → la función de
  decode y qué semáforo espera. Falta localizar `KS_wait` (mapear la tabla de tareas
  RTXC / el TCB de la tarea de audio).
- Herramienta nueva reutilizable: `gdbpoke.py --memreg IDX[+OFF]:LEN` (volcado de
  memoria relativo a registro) — lo que hizo posible leer la fuente y los
  descriptores del copy sin adivinar direcciones. `pctrace` ganó `memwpc=` (destino
  de stores en un PC concreto).

### 11.11 La caza RTXC en vivo: DOS hipótesis descartadas por medida, y el muro re-encuadrado

Sesión de traza en vivo con `gdb-multiarch` (SÍ está en el WSL, 15.1) + `pctrace`.
Play arrancado por **曲をシャッフル (Shuffle Songs)**, último ítem del menú principal
(japonés en este firmware), disco `~/6g/Emulti.img`, `AUDIOPACE=1`. Todo lo de abajo
**VERIFICADO** salvo lo marcado.

**Técnica que funciona (anotar):** `gdb.execute("continue")` desde el API Python de
gdb corre ASÍNCRONO contra el gdbstub de QEMU y falla ("Cannot execute while
running"); un **fichero de comandos `.gdb`** con `continue` de CLI (y bucles
`while`) SÍ bloquea. Todo el trabajo en vivo se hizo con `-batch -x script.gdb`.
Recetas en `~/dbg/` (WSL): `enum_bt.py` (volcado + backtrace de las 56 tareas de un
tiro, un solo attach), `catch.gdb` (caza el motor y lo sigue hasta su bloqueo),
`catch4.gdb` (lee el estado del HW de audio). **Un solo attach por captura** — el
ciclado attach/detach es lo que inducía el spin EIC (ver abajo).

**A) El spin EIC (0x22002e24) NO es el muro — DESCARTADO por medida no invasiva.**
La rutina de reloj/energía `0x22002dc0` (`iPodPowerProfile.txt`) reprograma un PLL
(`r0=CLK 0x3C500000`, `r1=EIC 0x39A00000` = `r0-0x2b00000`) y pollea EIC bit0
(`0x22002e24`) y bit12 (`0x22002e48`) con IRQs enmascaradas; EIC sin implementar lee
0 → spin eterno. **`pctrace pc=0x22002dc0`/`pc=0x22002e20` en un play limpio (sin
gdb): hits=0.** La rutina NUNCA se entra durante la reproducción. El spin que
capturó la sesión anterior era 100% artefacto del ciclado de gdb (un IRQ perdido en
un detach deja la rutina de reloj a medias). **No modelar EIC.** El estado estable
limpio es WFI idle en `0x22003b0c–0x22003b30`, cpsr con IRQs HABILITADAS.

**B) El muro NO es un periférico que falte ni uno que devuelva mal — DESCARTADO.**
`-d unimp,guest_errors` durante el play: los ÚNICOS accesos no modelados son 43
**escrituras** de config a GPIO `+0x200` (setup de IRQ de rueda/botones, no lecturas
que condicionen). **Cero** accesos a AES / SHA / ADM(`0x39/0x3F`) / SM1-window-alto
/ io-floor. El decode no alcanza ningún bloque sin modelar. `pctrace` de la cadena:
`audio_engine 0x0808170c`, `mixer 0x0809a74c`, `copyPCM 0x08016e44` corren **exactamente
1 vez**; `copy32 (PCM real) 0x08016eb4` = **0**. `KS_ISRSignal 0x22001ca4` = **7621
veces** (las ISR SÍ disparan). SM1: `SM1TRACE` en play = **0 accesos tras el SELECT**
(SM1 no se pollea durante el streaming); `catch4.gdb` en idle: SM1 `STAT(+0xa98)=0`,
`RUN(+0x400)=0` — ni siquiera "powered". SM1 es irrelevante en el camino de play.

**C) La salida de bajo nivel FUNCIONA — idéntica a Rockbox (VERIFICADO por registros).**
`catch4.gdb` en el idle de play leyó el HW de audio: **DMAC0 canal 1** activo —
`SRC=0x22009c5c` (ping-pong IRAM), `DEST=0x3ca00010` (FIFO del I2S), `LLI=0x22010020`
(cadena continua), `CTRL=0x84249800` (bit I=terminal-count IRQ), `CONF=0x8a81`
(E=1 habilitado, `dest_id=0xA`=IIS0_TX, flow=1 mem→peri, ITC=1). I2S `TXCOM=6` (RUN),
`CLKDIV=0x110`. Es **exactamente** el mecanismo que usa Rockbox (LLI-chain +
terminal-count IRQ 16) y que ya suena. DMAC1 sin usar. **El muro NO está aguas abajo
del ping-pong.** La `SM1 POS` avanzó a 0x27d800 (≈2.6M): el I2S drena silencio de
punta a punta desde el arranque (por eso `SM1pos` en `StartPlayback` ya era 1.97M ≈
44 s de silencio de boot).

**D) El motor de audio corre en la TAREA 0x25 (VERIFICADO por breakpoint).** `catch.gdb`
rompió en `0x0808170c`: corre en task `0x25` (worker MeCCA, entry `0x080833d0`),
llamado desde el driver IRAM de audio (`lr=0x22006304`, IRAM `0x22006xxx` = shadow
DRAM `0x07ffbxxx`; la "tarea de audio 0x07ffb2d0" de §11.8 es IRAM `0x22006118`).
Tras su ÚNICA corrida, task `0x25` bloquea en **buzón/exchange 0x52** vía servicio
**opcode 14** (wrapper `0x22004028`, un `KS_receive` de mailbox; el handler
`0x22000950` indexa la tabla de buzones en **`0x0897783c` stride 32**, distinta de la
tabla de semáforos opcode-1 `0x0897fb1c`). El copy `0x08016e44` corre 1 vez y SALTA
por `[r7+5]=0` (PCM no listo) — no por la posición SM1.

**E) Mapa de tareas RTXC en runtime (56 TCB, VERIFICADO).** `enum_bt.py`: array de TCB
`0x0897ff70` stride 52 (`+12` entry, `+16` savedSP, `+28` estado, `+32` tasknum; el
TCB no guarda puntero al nombre). Estados vistos: `0x40`=espera-semáforo (opcode 1,
resumePC `0x2200433c`), `0x08`/`0x20`/`0x04`/`0x01`=otras esperas (opcodes 14/4/…),
`0x00`=libre/corriendo. ~30 workers genéricos `0x080833d0`. Piezas clave:
- **task 0x20** = tarea de StreamCache/media (bt `0x08171500→0x081f39a4→0x081d0a2c→…→
  0x081fed34`), **idle esperando RECIBIR en exchange 6** (opcode 4). Es un consumidor
  ocioso, no "el productor bloqueado" que creía §11.10.
- **task 0x25** = motor/driver de audio, bloqueado en exchange 0x52 tras 1 corrida.
- Muchos workers parados en **`0x080c8998`** (retorno de `bl 0x8033d8c` = el `KS_wait`
  del RPC síncrono `0x080c8968`): hacen una petición y esperan la respuesta. El RPC
  `0x080c8968` es genérico (get-event `0x8033b70` → post `0x80cc0e4` → signal
  `0x8033af8` → wait reply): corre ~3728 veces en 18 s de play, NO es específico de
  audio.

**F) El disco NO es el problema.** La pista de `Emulti.img` es un **M4A/ALAC válido**
(`ftyp`@0x818004, `moov`, `mdat`@0x824824, fourcc `alac`) — contenedor MPEG-4 bien
formado, no corrupto. El códec ALAC se inicializa (§11.9); el "ID3" hallado en el
scan es ruido coincidente.

**RE-ENCUADRE DEL MURO (medido, honesto):** en el punto de bloqueo el sistema es un
**deadlock de mensajería MeCCA**: TODAS las tareas están bloqueadas en exchanges/
buzones inter-tarea, y NINGUNA está polleando un periférico. Por tanto **ningún
cambio del modelo de un periférico puede desbloquearlo directamente** — lo que falta
es un *mensaje* (un `KS_send`/`KS_signal` de tarea a tarea) o un paso del setup
SÍNCRONO que debió despachar el primer decode y no lo hizo. Las ISR que sí disparan
(tick de timer + DMA IRQ 16) despiertan al driver de salida de bajo nivel (rellena
silencio) pero **no** disparan la tarea de decode. El decode nunca produce la trama 0.
El deadlock es DURO (§11.8: 0 bloques nuevos tras 367 ms), no un timeout lento.

**El siguiente nodo, preciso:** trazar el setup SÍNCRONO de reproducción desde
`StartPlayback 0x08150d84` (media player `0x0815xxxx` → apertura del M4A → init del
códec ALAC → **el punto donde debería encolar/señalar el primer decode**) y hallar la
rama que NO despacha esa petición. Pista de no-determinismo: la sesión anterior
alcanzó el handshake SM1 (33 accesos, §11.6) y ésta no (0 accesos) — el mismo play
se atasca en puntos distintos → **hay una carrera/timing** (candidato: el instante
del primer IRQ 16 respecto al setup). Método sugerido: breakpoint en el `KS_receive`
del worker de decode y en `0x080c8968`/`0x8033af8` filtrando por el exchange que toca
el media player; o comparar el árbol de `pctrace range=` de un play que progresa vs.
uno que se atasca.

**Experimento de timing PROBADO y DESCARTADO (esta sesión):** hipótesis "el primer
IRQ 16 del DMA de audio llega demasiado pronto y el setup del decode pierde/atropella
la señal". Se añadió temporalmente un knob `AUDIOPACE_START_MS` que retrasa el primer
tick de drain (y con él el primer terminal-count IRQ 16) N ms; probado con 1500 ms.
Resultado: **no confirma** — no cambia el muro (`copy32=0` sigue), y peor, retrasar la
salida ~1.5 s tiende a **inducir resets** (el retailOS parece vigilar el avance de la
reproducción y se resetea si se estanca). Coherente con el diagnóstico: el decode NO
espera el IRQ 16 (la salida de bajo nivel va sola, §C) sino un MENSAJE de tarea aguas
arriba. El knob se **revirtió** (árbol limpio). El timing del IRQ 16 no es la palanca;
el siguiente esfuerzo va al setup síncrono del decode, no al pacing del DMA.

**Higiene:** cero cambios en fuentes del modelo (sólo lecturas/breakpoints gdb y
`pctrace`, ya en el árbol). Árbol byte-idéntico al baseline. Rockbox/navtest intactos.
Instrumentación de sesión en `~/dbg/` del WSL (fuera del repo).

### 11.12 El muro re-caracterizado por MEDIDA: NO es un evento de hardware — es un deadlock de rendezvous puro (y correcciones a §11.6/§11.8/§11.F)

Sesión de medición dedicada al grafo de espera y al setup del decode. Play limpio de
`~/6g/Emulti.img`, `AUDIOPACE=1`, disparado por Shuffle Songs. Todo lo de abajo
**VERIFICADO** por lectura/breakpoint gdb (no invasivo) o instrumentación temporal del
modelo (revertida). Recetas en `~/dbg/` (`probe6.sh`=wgraph, `probe7.sh`=HW+timers,
`probe8.sh`=codec-setup, `probe9.sh`=tasas de ISR; `catch5.gdb`, `catch_setup.gdb`).

**A) La pista es AAC-LC, NO ALAC (corrige §11.F). Y el códec SÍ se configura (refuta la
hipótesis "AAC → rama default").** Las 6 pistas de `Emulti.img` son `mp4a` en el `stsd`
(esds objectTypeIndication `0x40`, ASC `0x1210` = AAC-LC 44100/estéreo), medido con
`ffprobe`+volcado del `stsd`. La cadena "alac" que vieron sesiones previas es la tabla
fourcc del demuxer + bytes de `mdat`, no la sample entry. **CORRECCIÓN de dirección:** el
getter `0x08015860`→IRAM `0x22006078` devuelve la struct de estado en **`0x220106dc`**
(no `0x22008c4c`, que es sólo un latch de init interno); el tipo de códec vive en
`0x22010734` (+0x58) y el gate en `0x22010723` (+0x47). Con breakpoints de baja
perturbación (`probe8.sh`) se MIDIÓ en vivo durante el setup: `CODEC_SETUP`
(IRAM `0x22006574`) corre con **type=0x01**, `GATE_SET` (`0x220065ac`) pone gate=1, y el
motor `0x0808170c` lee **gate=1, type=1** → toma el **caso 1** del switch (`0x0808182c`),
que SÍ cablea el pipeline. **El tipo 1 es un caso manejado; el motor se configura bien.**
(Las lecturas snapshot que daban type=0xff/gate=0 son *post-teardown*: el estado se
reinicializa cuando la reproducción falla — un espejismo. Medir en vivo, no en el atasco.)

**B) El grafo de espera exacto (regime A, deadlock estable, resets=0, `wgraph.py`).**
- **t2a** (decodificador AAC, cluster `0x08123xxx`) BLOQUEADO recibiendo en **buzón 0x09**
  (`0x08977964`); `M09.seq≈52` → StreamCache le sirvió ~52 trozos. Está **hambriento**
  (esperando input), no fallando al decodificar.
- **t25** (motor de audio) BLOQUEADO recibiendo en **buzón 0x52** (`0x08978284`);
  `M52.seq≈18-20`. Corrió su setup UNA vez (type 1) y espera comandos.
- **t20** (StreamCache) BLOQUEADO recibiendo en **exchange 6** (`sema[6]=0x20`,
  `0x0897fb34`). Es el SERVIDOR: dueño de los buzones 0x09/0x52/0x4e.
- t20 sirvió ~52/18 veces y luego se durmió en exchange 6 esperando el disparo #53. El
  punto de atasco es **reproducible/determinista** (M09≈52-53, M52≈18-20 en 4 corridas).

**C) NO hay evento de hardware perdido ni mal temporizado — DESCARTADO por medida directa
(`catch5.gdb` en el atasco).** VIC0 `RawIntr=0` (CERO IRQ pendiente), `IntEnable=0x21a7010f`
(irq16 DMAC0 e irq29 ATA habilitados). DMAC0 `RawIntTC=0`, **ch0 (almacenamiento) IDLE
(E=0)** — el disco ya está leído, no hay DMA colgado; ch1 (audio) sigue E=1 dando vueltas
a su LLI. ATA quiescente (transfer completo). **No hay ninguna interrupción pendiente que
el guest no esté sirviendo.**

**D) Las ISR que importan SÍ disparan, continuamente, DENTRO del atasco — DESCARTA "tick
muerto"/"IRQ perdida" (instrumentación temporal `IRQDBG`, `probe9.sh`).** El **tick RTXC
de 1 kHz** (timer B, 16-bit, ECLK/2, DATA0=6000, INT_EN, IRQ 8) dispara **~1000/s** sin
parar durante el deadlock. La **IRQ 16 de audio (DMAC0 ch1)** dispara **~50/s** (tempo
real, ~43 ráfagas/s de 2048 muestras). El almacenamiento (ch0) está parado (disco leído).
Es decir: el scheduler RTXC recibe su tick 1000 veces por segundo y el driver de salida su
IRQ 50 veces por segundo, **y aun así nadie señala el exchange 6.**

**RE-ENCUADRE FINAL (medido, honesto):** el muro **NO** encaja en ninguno de los tres casos
de "arreglo en el modelo por evento de periférico": no hay evento externo mal temporizado
(C: sin IRQ pendiente), no es que el tick/DMA falte (D: ambos vivos), y no hay latencia de
hardware que reproducir (el disco/ATA/SM1 están correctos y ociosos). Es un **deadlock de
rendezvous inter-tarea PURO**: el `KS_send`/`signal` al exchange 6 que despertaría a
StreamCache proviene de una tarea del **camino de datos** que está ella misma bloqueada
(t2a hambrienta ↔ t20 en ex6 ↔ t25 en buzón 0x52). El primer dominó —la PRIMERA muestra de
PCM (`copy32 0x08016eb4` = 0 siempre)— nunca se produce, así que el ciclo
productor↔consumidor nunca arranca, y ni el tick de 1 kHz ni la IRQ de audio lo rompen
(sus manejadores no tocan el exchange 6 mientras el buffer drenado sea silencio).

**Lo que queda DESCONOCIDO (el obstáculo exacto):** QUIÉN debería señalar el exchange 6 en
el arranque en frío y por qué su tarea no corre. Es un problema de protocolo del framework
MeCCA (bootstrap del pipeline), no de un registro/temporización de periférico. Como la
carrera regime-A/regime-B es sensible a la velocidad del guest (instrumentación pesada →
regime B), atrapar el `send`-a-exchange-6 en vivo exige una sonda de MUY baja perturbación
(los watchpoints sobre la página de datos del kernel `0x0897xxxx`, que es caliente, empujan
a regime B). Vía sugerida: localizar el veneer del primitivo `send-exchange` de RTXC (opcode
del KS, tabla en `0x22003620`) y ponerle un breakpoint condicionado al índice 6 —
suficientemente raro para no perturbar— para leer el LR del emisor de los ~52 señales que SÍ
ocurren antes del atasco, y ver por qué se detiene la #53.

**Higiene:** instrumentación `IRQDBG` (contadores en `hw/dma/pl080.c` y
`hw/arm/ipod_classic/ipod_classic_timer.c`) **revertida**; árbol byte-idéntico al baseline;
recompila y enlaza limpio. Cero PCM inyectado, cero parche de firmware. `pctrace` intacto.
Rockbox/navtest sin tocar (los caminos medidos son sólo lecturas del retailOS).

### 11.13 El lado CONSUMIDOR re-caracterizado: el motor NUNCA recibe disparo de render; el muro es un bootstrap circular simétrico del primer ciclo

Sesión con la lectura de registros no invasiva ya operativa (`sendex=`/`regpc=`,
§ commit `0a0ea0365`). Play limpio de `~/6g/Emulti.img`, `AUDIOPACE=1`, Shuffle
Songs. Todo **VERIFICADO** por lectura/breakpoint gdb no invasivo o por contadores
`pctrace pc=` (cero perturbación medida: `resets=0`). Recetas nuevas en `~/dbg/`
(fuera del repo): `runt2a.sh`+`t2a_probe.py` (grafo+pila unwound), `runcounts.sh`
(perfil del decode), `runmbox.sh` (atribución de emisores por LR/sp), `runtree.sh`
(árbol del decode por `range=`), `t2a_probe2/3.py`. Árbol del modelo **sin tocar**.

**A) El MOTOR de audio (t25, consumidor) NUNCA recibe un disparo de render — VERIFICADO.**
La espera de t25 es un `recv` de buzón 0x52 CON watchdog de 3 s: pila
`0808dfa8→22002f84→22006824→[recv 0x22006814]→0x080b3e78→0x805c518`, timeout
`r1=0xbb8=3000ms`. Sonda `sendex=0x22003f7c:0:0x52` (gatea el reg r0==0x52 en la
cabecera-TB del primitivo *select-mailbox* `0x22003f7c`, hardcodeado con el nº de
buzón en r0): **22 matches en el play, TODOS son el propio `recv` de t25**
(`lr=0x805c578`, `r1=0xbb8`) disparándose **cada ~3.00 s exactos** (timeout) y
re-esperando. **CERO envíos.** Nadie manda jamás un comando de render al motor.
Tras 3 s el motor hace **teardown** del códec (`type[+0x58]→0xff`): ése es el
"espejismo post-teardown" de §11.12-A y la fuente del no-determinismo entre
sesiones (según ataques antes o después del watchdog de 3 s se ve el estado limpio
o el desmontado).

**B) La ÚNICA corrida del motor (setup) intenta renderizar y sale en vacío — VERIFICADO.**
El motor `0x0808170c` (caso 1, gate=1, type=1 de §11.12-A) llama al mixer
`0x0809a74c` **8 veces** (`0x8081a9c..0x8081cb0`), pero **todos los `copy` SALTAN**
(`[r7+5]=0`, PCM no listo) → `copy32 0x08016eb4 = 0`. Su primer render corre
**demasiado pronto** (el decoder aún no ha producido la trama 0) y **no hay segundo
render** (sin disparo). `copy32=0` es invariante en todas las corridas.

**C) El DECODER (t2a) es un actor de mensajes; su decode de datos NUNCA se dispara — VERIFICADO.**
`pctrace` del cluster: el *dispatcher* `0x08123dcc` conmuta por `[msg+4]`;
el manejador **tipo-12** `0x8123970` decodifica (`bl 0x8123bfc`) **sólo si**
`[msg+8]==obj+84` (el puerto de decode) **y** `[[obj+52]+96]!=0`. Contadores en un
play completo: **`h12 (0x8123970) = 0`** (el disparo "buffer de salida disponible /
render-pull" NUNCA llega), `dstep (0x8123284) = 1`, `dwork (0x8123bfc) = 1`. Y el
`range=` de esa única corrida del decode **NO toca AAC-math**: los bloques
`0x08324xxx/0x08325xxx` que parecían "códec" son **I2C0 (`0x3C600000`)** hablando con
el chip **CS42L55** (`0x08325634`: spin en `[I2C0+0x10]`, `[I2C0+0x20]|=0x3f00`),
NO IMDCT/huffman. **Corrección:** el decode SW de AAC **no llega a correr** (H1 de
§11.9 se mantiene); lo que corre una vez es la config del chip códec por I2C, que
**completa** (mismo camino que usa Rockbox). El decode-gate `[[obj+52]+96]` se
midió **=1** en un snapshot (entrada lista) — el decoder está listo pero sin disparo.

**D) Mapa KS/mensajería consolidado (VERIFICADO por desensamblado).** Tabla de
opcodes del dispatcher KS `@0x2200064c`: **op1**=WAIT-sema, **op2**=SIGNAL-tag,
**op4**=RECV-exch, **op5**=SEND-exch, **op14**=RECV-mbox. *Select-mailbox*
`0x22003f7c` (r0=nº buzón, es cabecera-TB → `sendex`-able). Veneers del lazo de
render IRAM: `0x22003840→0x0808170c` (motor), `0x22003868→0x080b3e78` (su recv con
timeout). Dispatcher del decoder `0x08123dcc` (tipo-12=`0x8123970` decode-gated,
tipo-19=`0x8123cfc` switch sobre códigos `0x52800000+n`). La familia de verbos de
buzón-9 del decoder (`0x8025ac4` send, `0x8025a04` recv, …) está hardcodeada a mbox 9.

**E) EL MURO, re-caracterizado (medido): un DEADLOCK CIRCULAR SIMÉTRICO del primer ciclo.**
Las dos mitades se bloquean mutuamente:
- El motor renderiza PCM real **⟸** disparo de render (mbox 0x52) **⟸** la salida
  consume PCM real (devuelve crédito) **⟸** el motor renderiza PCM real.
- El decoder decodea la trama 0 **⟸** render-pull/buffer-de-salida (tipo-12) **⟸**
  el motor renderiza **⟸** el decoder decodea la trama 0.

Ningún lado está **cebado** para arrancar sin el otro. El conductor (StreamCache
t20) manda datos al decoder (mbox 9) y comandos al motor (mbox 0x52) **a medida que
fluye el crédito**; t20 está bloqueado en exchange 6 esperando crédito de su
upstream (t0e←t16), y ese crédito sólo vuelve cuando la salida consume PCM real.
**El decoder está BLOQUEADO, no lento** — tiempo infinito no lo arranca. `copy32=0`
de punta a punta.

**F) Por qué NO se halló arreglo de modelo (re-confirmado, honesto).** En el atasco
**ninguna tarea pollea hardware** (grafo de espera: todas en mbox/exchange). Los
ÚNICOS toques de HW del camino de setup — **posición SM1** (avanza, correcta) y la
**config I2C del CS42L55** (completa, idéntica al camino que Rockbox usa y suena) —
son **correctos**. Cero periférico sin modelar (`-d unimp` = sólo 43 writes GPIO,
§11.11-B); IRQ16 (~50/s) y tick de 1 kHz disparan de continuo (§11.12-D). Por tanto
**ningún valor de un periférico modelado ni evento de HW en el lazo del deadlock
puede desbloquearlo**. Un valor de HW sólo podría importar si hiciera **divergir una
rama AGUAS ARRIBA en el cebado**; se buscó (SM1, I2S, DMA, I2C, timers) y **no se
localizó** ese valor en 6 sesiones — el cebado que rompe el ciclo vive dentro del
**protocolo asíncrono de buffers/mensajes de MeCCA**, no en un registro.

**VEREDICTO HONESTO (esta sesión): NO suena.** El muro es un **deadlock de bootstrap
circular del pipeline MeCCA**, confirmado por medida como libre de todo valor/evento
de HW ausente o erróneo *dentro del lazo*. Esta sesión **no** halló un arreglo de
modelo localizado (coherente con las 5 previas), pero **afinó el mecanismo**: el
primer dominó es el **CONSUMIDOR** (el motor nunca se dispara; watchdog de 3 s →
teardown), no sólo el decoder; y es **simétricamente circular** (ninguna mitad
cebada). Para cerrarlo haría falta: **(a)** localizar el paso de **cebado** exacto de
MeCCA — quién debe emitir el primer render-pull (tipo-12) al decoder y/o el primer
comando de render (mbox 0x52) al motor **sin** esperar crédito — probablemente contra
una **referencia de HW** o el estado que deja el **bootloader** real (nuestro
`-kernel` lo salta); **(b)** si tal cebado resultara depender de un valor que el
firmware lee mal al arranque, corregir **ese** valor (empujón de modelo legítimo,
aún no hallado); **(c)** en ausencia de (a)/(b), el muro es **intrínseco** a una
emulación fiel de este bootstrap concurrente y sólo lo abriría un **HLE del
decoder/motor** o un modelo de **timing de instrucción** (icount) que reordene el
scheduling — con la salvedad del auditor de que el determinismo por sí solo no basta.

**Higiene:** cero cambios en fuentes del modelo; árbol byte-idéntico salvo esta
entrada de wiki. `pctrace` intacto (ya en el árbol desde `0a0ea0365`).
Instrumentación de sesión en `~/dbg/` del WSL. Rockbox/navtest/5G sin tocar.

### 11.14 El cebado, cerrado por medida: el lado de ENTRADA está sano; el muro es SÓLO el disparo de SALIDA; y el candidato SM1+0xa98 bit12 REFUTADO

Sesión de cierre del nodo de cebado. Se ejecutó el experimento que §11.12 dejó
propuesto y que nadie había corrido (la atribución de emisores del **exchange 6**),
se desensambló el lazo de render del motor a nivel de instrucción, y se **probó y
descartó por medida** un candidato de valor de HW nuevo. Todo **VERIFICADO** por
`pctrace` no invasivo (`sendex=`/`pc=`, disco `Emulti.img`, `AUDIOPACE=1`, Shuffle
Songs). Recetas nuevas en `~/dbg/`: `runex6.sh`, `runsm1v3.sh`. Árbol byte-idéntico.

**A) La atribución de emisores del exchange 6 (NUEVO — cierra la incógnita de §11.12/§11.13-E).**
Sonda `sendex=0x22004138:3:0x6` (el *post* KS `0x22004138` lleva `r3`=índice de
exchange, `r2`=opcode; VERIFICADO desensamblando el verbo del decoder `0x8025ac4`:
`mov r3,#6; mov r2,#5; bl 0x8015740→0x22004138`). Resultado de un play:
**57 posts al exchange 6, TODOS `r2=5` (op5 SEND-exch), TODOS desde la familia de
verbos del decoder** `0x08025xxx` (por LR: `0x8025b18`×27, `0x8025a5c`×25,
`0x80258a4`×3, `0x8025960`×1, `0x8025904`×1). **Ni un solo post** viene de una ISR,
de una tarea de disco *upstream*, ni del camino de consumo de salida.

⇒ **CORRIGE §11.13-E:** no existe "crédito de upstream (t0e←t16)" que fluya por el
exchange 6. El exchange 6 es SÓLO el canal del **decoder → StreamCache** (peticiones
de entrada; StreamCache responde por mbox 9). El lazo de ENTRADA
decoder↔StreamCache es **autónomo y está SANO** (57 intercambios, coincide con
`M09.seq≈52`). Se llena hasta su tope y se detiene esperando que el consumidor
(el motor) tire — que nunca ocurre. **El muro NO está en la entrada.**

**B) El muro es categóricamente el DISPARO DE SALIDA — re-confirmado.** En el mismo
play: `sendex=0x22003f7c:0:0x52` = **25 matches, todos el propio `recv` del motor**
(cero envíos a mbox 0x52); `copy32 0x08016eb4 = 0`; `engine=1, mixer=1, copyfn=1,
disp=6, h12=0`. Idéntico a §11.13. **Nadie manda jamás el comando de render (mbox
0x52) al motor**, y el lado de entrada demuestra que **no es por falta de datos**.

**C) El lazo de render del motor, a nivel de instrucción (VERIFICADO por desensamblado).**
La tarea 0x25 (driver de audio, IRAM) llama al motor **una vez** en el setup
(`0x22006300: bl 0x22003840→0x0808170c`) y luego **espera**:
`0x22006814: recv(obj=0x220106d0, timeout=0xbb8=3000ms)` vía `0x22003868→0x080b3e78`;
en timeout retorna −1 y el driver hace **teardown**. El buzón del motor es un campo
de la struct `0x220106d0`; **nadie postea a él** (§B). El disparo de render es un
**mensaje de tarea a tarea** (StreamCache t20 → motor t25), cuya rama de envío en
StreamCache **nunca se alcanza** (§11.13-B: 0 `select` de mbox 0x52 para enviar).

**D) Candidato de valor de HW NUEVO — PROBADO y REFUTADO por medida.** El cuerpo de
setup `0x0808af58` lee **SM1+0xa98** (getter `0x080a7638`, `r1=0` → `[obj+0xa98]`,
`obj` = `handleTable[0x08ade488]` = base SM1) y **gatea sobre el bit 12 (0x1000)** un
bloque que escribe **SM1+0x804=1** (`0x80a35a0`) y **SM1+0x858=1** (`0x809a72c`,
estrobos de "enable" del motor de audio). Nuestro modelo SM1 sólo pone el **bit 2**
(clock-ready) en +0xa98 → bit 12 siempre 0 → el bloque de enable se salta. Candidato
plausible que las sesiones previas (centradas en el bit 2) no habían tocado.
**Medida (flag `SM1_ENGINE=1` que fuerza el bit 12, revertida):**
`copy32 = 0` (sin cambio) **y** `setupbody 0x0808af58 hits = 0` — el lector del bit 12
**no está en el camino de play** (es init de arranque del subsistema de audio, no el
cebado por-reproducción); además los dos writes gateados son **latches inertes** en el
modelo (nada del render depende de SM1+0x804/+0x858). **No es el arreglo.** Revertido;
árbol byte-idéntico (`git diff` vacío, recompila y enlaza limpio).

**VEREDICTO (esta sesión): NO suena — muro ESTRUCTURAL, cerrado por medida.** El
cebado del primer ciclo MeCCA **no depende de ningún valor/registro/evento de HW ni
de estado dejado por el bootloader**: es un **mensaje inter-tarea** (el comando de
render mbox 0x52 de StreamCache al motor, y simétricamente el render-pull tipo-12 al
decoder) cuya rama emisora está gateada por la condición **circular** "salida consumió
PCM real / hay trama decodificada" — y `copy32=0` la mantiene falsa de punta a punta.
La atribución del exchange 6 (§A) elimina la última hipótesis de "crédito externo":
la entrada está sana; **falta exclusivamente el disparo de salida en frío**, y ese
disparo no lee nada corregible. Coherente con las 6 sesiones previas y afinado.

**La vía real (documentada, no es de modelo):** un **HLE del cebado** que emita en
frío **ambos** disparos —el primer mbox 0x52 al motor (`recv obj 0x220106d0`) **y** el
primer tipo-12 al decoder (`0x08123970`, gate `[[obj+52]+96]!=0` ya =1)— para romper
la simetría; el PCM lo produciría entonces el decoder del huésped. Es un shim del
protocolo MeCCA, **no** un valor de HW: por eso no cabe como "arreglo de modelo". Un
modelo de `icount` (QEMU 9.x) podría además explorar la carrera regime-A/B (§11.11)
por si el orden concurrente real difiere, pero el determinismo por sí solo no garantiza
que aparezca el emisor ausente.

**Higiene:** experimento SM1 bit12 **revertido**; árbol byte-idéntico al baseline
(`git diff` vacío) salvo esta entrada de wiki; recompila limpio. `pctrace` intacto.
Recetas de sesión en `~/dbg/` (`runex6.sh`, `runsm1v3.sh`), fuera del repo.
Rockbox/navtest/5G sin tocar (los caminos medidos son sólo lecturas del retailOS).

### 11.15 El shim HLE del tipo-12: IMPLEMENTADO y DISPARADO, pero el tipo-12 NO es el muro (corrección medida a §11.13/§11.14)

Sesión de **implementación** del shim HLE que las siete previas dejaron
especificado. Se construyó el disparo del **tipo-12 render-pull** al decoder y se
midió su efecto con navegación FIABLE (la pieza que faltaba). VA = off +
`0x07FF51B8`. Todo **VERIFICADO** por `pctrace` (contadores `pc=`/`sendex=` +
`memw`) y por la traza del propio shim. Disco `Emulti.img`, `AUDIOPACE=1`.

**Lo implementado (queda en el árbol, GATEADO, build por defecto byte-idéntico).**
`hw/arm/ipod_classic/ipod_classic_hle_audio.c` (+ `.h`), cableado en la máquina y
disparado por el bit *run* del I2S. Con `HLE_AUDIO_KICK` sin poner **no instala
nada** (init retorna antes de tocar estado; el hook del I2S es un no-op). Con
`HLE_AUDIO_KICK=1`: un timer de reloj virtual vigila la puerta render-pull del
decoder (`decoder_obj` = heap `0x0ba9a8a4`, VALIDADO por su vtable `[obj+0]==
0x088d1948`; gate `[[obj+0x34]+0x60]`); cuando la puerta es !=0 (primed) y la CPU
está en el **idle loop RTXC** (WFI, `0x22003b0c–0x22003b30`, para no pisar el
contexto de ninguna tarea viva), redirige el contexto idle a llamar al
**dispatcher del huésped `0x08123dcc`(decoder_obj, msg)** con un mensaje de 12
bytes (`[msg+4]=12`, `[msg+8]=obj+0x54`) escrito bajo la pila idle y LR de vuelta
al idle. Cero PCM inyectado; el mensaje es exactamente el que el handler
`0x08123970` exige. (`HLE_AUDIO_KICK_FORCE=1` = knob de depuración que dispara aun
con gate=0, para ejercitar la entrega; `HLE_AUDIO_KICK_TRACE=1` traza.)

**A) EL DECODER SÍ SE CEBA — y hacía falta NAVEGACIÓN FIABLE (corrige §11.11–14).**
Las sesiones previas medían con un SELECT flojo (además, el `grep StartPlayback`
casa con la línea *watch* que `pctrace` imprime al arrancar → falso positivo → una
sola pulsación de SELECT). Eso las dejaba en **régimen B (infra-progresado)**:
`disp=2`, gate=0, el decoder ni se configura. Con SELECT FIABLE (`iremote press
ret -u -w q,e --region right`, reintenta hasta que cambia la pantalla) el play
**arranca de verdad y el decoder se configura ENTERO**: MEDIDO en varias corridas
`disp=7`, `ex6≈61–134` (entrada cargada), y la puerta render-pull
`[[obj+0x34]+0x60]` mide **`0x4` (!=0 = primed)**. El cebado del decoder **existe
y es alcanzable** — la premisa "el decoder está listo" de §11.13 se confirma.

**B) EL FIRMWARE EMITE EL TIPO-12 ÉL MISMO (refuta "nadie lo emite" de §11.13/§11.14).**
En cuanto el play progresa de verdad, MEDIDO sin shim (HLE off, nav fiable):
`h12 (0x08123970) = 1`, `gateconsume (0x081239bc, el str que consume la puerta,
sólo alcanzable en la rama de decode) = 1`, `dwork (0x08123bfc) = 3`. Es decir: el
retailOS **SÍ despacha el render-pull tipo-12** y **entra en la rama de decode**.
El `h12=0` de las 7 sesiones era un **artefacto del régimen B** (bajo
navegación floja el pipeline no llega ahí). El shim, con gate=`0x4`, también
dispara (traza "emitted type-12 ... from idle pc=0x22003b20"); pero su tipo-12 es
**redundante** con el que el firmware ya emite.

**C) EL MURO REAL: el tipo-12 es INSUFICIENTE — no sale PCM aunque se decode
(hallazgo decisivo).** Con el decode disparado (por el firmware o por el shim,
`h12=1`, `gateconsume=1`, `dwork=3`), MEDIDO con `memw=0x08888000-0x0888c000` (la
fuente PCM que lee el copy, §11.8): **8192 stores, TODOS del memset
(`0x22000198`/`0x220001a0`), CERO del decoder.** `copy32 (0x08016eb4) = 0`
invariante; `mixer (0x0809a74c) = 1` (sólo el render de bring-up, que salta por
`[r7+5]=0`). **El tipo-12 NO produce PCM.** El `dwork 0x08123bfc` NO decodifica
inline: **postea** un mensaje async (`0x8105c14` tipo `0x3a98` → `0x8105524`) al
puerto de decode `obj+0x54`; los 3 posts se acumulan pero **la tarea de decode
AAC aguas abajo nunca produce PCM**. El muro está **un nivel más abajo del
tipo-12**, no en el tipo-12.

**VEREDICTO HONESTO (esta sesión): el retailOS NO suena.** El `hle_out.wav`
capturado sigue siendo el **clic de UI** (L==R 100%, ~20k muestras no nulas en
ráfagas — no música). Lo aportado, todo MEDIDO:
1. **Shim HLE del tipo-12 implementado, gateado, seguro** (no rompe el build por
   defecto; `resets` observados = watchdog del propio retailOS ante la
   reproducción estancada, no del shim) y con su **mecanismo de entrega validado**
   (dispara el dispatcher desde el idle sin corromper tareas vivas).
2. **El decoder SÍ se ceba** (gate `0x4`) con navegación fiable — el cebado no era
   el muro.
3. **El tipo-12 NO es el muro**: el firmware lo emite solo (`h12=1`), el decode
   entra en su rama (`gateconsume=1`, `dwork=3`) y **aun así `copy32=0` y la
   fuente del mixer sólo recibe memset**. Corrige la hipótesis central de las 7
   sesiones ("falta el tipo-12"): faltaba **navegación fiable** para verla, y una
   vez vista, el tipo-12 resulta **presente pero insuficiente**.

**El muro exacto que queda (para la próxima sesión):** la **tarea de decode AAC
async** —receptora de los posts de `0x08123bfc` al puerto `obj+0x54`— **no corre
o no escribe PCM**. Es un tercer nivel de rendezvous, aguas abajo del tipo-12 que
las 7 sesiones creían el último. Vías: (a) localizar esa tarea (quién recibe en
`obj+0x54`, su TCB/estado en el deadlock) y ver en qué se bloquea; (b) el shim ya
tiene el mecanismo de entrega genérico (redirección desde idle a una función del
huésped) — reutilizarlo para cebar esa tarea, o el render del motor (mbox 0x52,
cuyo payload sigue DESCONOCIDO, §mensaje). El PCM del huésped no aparece porque el
productor real (decode AAC) está parado, no porque falte el tipo-12.

**Higiene:** `HLE_AUDIO_KICK` off por defecto → build byte-idéntico al baseline;
Rockbox/navtest/5G intactos (el shim es inerte sin la env). Recetas de sesión en
`~/dbg/` (`runhle_*.sh`, `cap_hle.py`), fuera del repo.

### 11.16 `obj+0x54` es un TIMER (no una tarea AAC); la caza de la "tarea async" era un fantasma; el muro es el disparo de SALIDA en frío — VEREDICTO cerrado por examen VIVO del decodificador real

Sesión de resolución. Objetivo del workflow: "cazar la tarea AAC aguas abajo que
recibe los posts a `obj+0x54` y no produce PCM" (§11.15). Se **midió de nuevo con
navegación fiable** (4 capturas decode-confirmadas) y se **desensambló entera** la
cadena `dwork`. Resultado: la premisa del workflow es una **misidentificación**.
Todo **VERIFICADO** por desensamblado directo (`~/6g/osos.dis`, VA = off +
`0x07FF51B8`) o lectura gdb no invasiva de un play (disco `Emulti.img`,
`AUDIOPACE=1`, Shuffle Songs, SELECT fiable). Recetas nuevas en `~/dbg/`:
`runmeas.sh`, `runtrace.sh`, `runt2aexam.sh`, `t2a_examine.py`. Árbol
byte-idéntico al baseline (sólo lecturas; cero cambios de fuentes).

**A) `obj+0x54` es un TEMPORIZADOR de 15 s, no un canal de decode — VERIFICADO por desensamblado (confirma Report-1; REFUTA §11.15-C y la interpretación "actor drainer" de Report-2).**
`dwork 0x08123bfc` hace: `r1=0x3a98; r0=obj+0x54; bl 0x08105c14; b 0x08105524`.
Desensamblado de los dos destinos:
- `0x08105c14 → 0x08051b94`: `str r5,[r4,#4]` ⇒ **`port[+4] = 0x3a98`** (el
  INTERVALO, no un "tipo de mensaje").
- `0x08105524`: pone `port[+0x20]="run "` (`0x72756e20`) → `0x080578fc`, que:
  `NOW = 0x08033a40()`; `deadline = NOW + interval·125·8 = NOW + interval·1000`;
  `str deadline,[port+8]`; **inserta `port` en una lista enlazada ORDENADA POR
  DEADLINE** (cabecera en el bloque de control `0x08911488`; lazo de inserción
  `0x0805795c` que compara deadlines con `0x0826480c`); `port[+0x1c]=1` (encolado);
  y **señala el semáforo de la rueda** (`0x0806a8ac`, sema `0x08970050`).
Esto es una **inserción de rueda de temporizadores de libro**. `0x3a98 = 15000`
= intervalo en ms = **15 s**. `dwork` **ARMA un watchdog de 15 s**; no postea
ningún mensaje de decode. La "tarea que recibe en `obj+0x54`" es **t04**, el
**servicio de la rueda** (entry `0x080833d0`, resumePC `0x22004394` op1b-WAITsema,
parkeada en sema `0x08970050`). **La tarea AAC async que este workflow fue creado
para cazar NO EXISTE** — `obj+0x54`/`0x3a98` es un falso rastro. (§11.15-C y
Report-2 malinterpretaron el arme del watchdog como un post async; Report-1
acertó.)

**B) La contradicción h12=0 (Report-1) vs h12=1 (§11.15) = la carrera regime-A/B MÁS el watchdog de 15 s — RESUELTA por medida.**
Con navegación fiable, MEDIDO (`runmeas.sh`, `pctrace pc=`/`sendex=`):
- **Corrida A (régimen A):** `disp=7, h12=1, gateconsume=1, dwork=3, ex6=57`,
  `copy32=0`. La **traza temporal** revela lo decisivo: setup a `+74.7 s` (disp
  #1-2, dwork #1-2 arman watchdogs) y un primer render-pull tipo-12 con **gate=0**
  (`h12else`), luego un **STALL de 15.13 s EXACTOS**, y a `+89.87 s` **el watchdog
  vence** → ráfaga (disp #3-6, `h12 #1` con gate ya puesto, `gateconsume`, dwork
  #3) → el motor corre su bring-up (`engine=1, mixer=1`) a `+90.2 s` → **`copy32=0`**.
- **Corrida B (régimen B):** `disp=6, h12=0, dwork=2, ex6=87`, `copy32=0`, y el tag
  del timer pasa a `"stop"` (`0x73746f70`, teardown).
⇒ **`h12=1` SÍ se alcanza (§11.15 acertó), pero SÓLO tras el stall+watchdog de
15 s**; las capturas de Report-1 (`<15 s`, como el propio Report-1 anotó) medían
ANTES del vencimiento → `h12=0`. Y **el resultado es INVARIANTE en ambos
regímenes: `copy32=0`, cero PCM.** La carrera no cambia el desenlace. El watchdog
de 15 s (y su teardown) SON el mecanismo de **recuperación-de-estancamiento del
propio retailOS**: su existencia y disparo **prueban que el pipeline no arranca en
frío** — el empujón inmediato de la primera trama que el HW real provee está
ausente, y ni siquiera el watchdog cierra el lazo.

**C) PRIMER examen VIVO del decodificador AAC REAL (t2a) y del consumidor/conductor — deadlock de rendezvous PURO, confirmado en la propia tarea (`t2a_examine.py`).**
Snapshot pre-watchdog (régimen A, gate primado): `decoder_obj=0x0ba9a8a4`
(vtable `0x088d1948`), `ctx=[obj+0x34]=0x0ba90c50`, **gate `[ctx+0x60]=0x4`
(primado)**. Estado de las tres tareas del lazo (todas worker entry `0x080833d0`):
- **t2a (DECODER AAC):** `st=08`, resumePC `0x22004028` = **op14-RECVmbox en
  buzón 9 (ENTRADA)**. Cadena de retorno `0x08025b04 → 0x081232a4 (dstep) →
  0x08123da8 (tipo-19) → 0x08123e20`: está **dentro de `dstep 0x08123284`**,
  habiendo pedido entrada por exchange 6, **bloqueado esperando el chunk en
  buzón 9**. NUNCA llega al decode por-trama.
- **t25 (MOTOR de audio):** `st=08`, op14-RECVmbox en **buzón 0x52 (comando de
  render)**, cadena `0x0805c578 → 0x080b3e80 (recv con timeout 3 s) → 0x22006824`.
  **Nadie le manda jamás el comando de render.**
- **t20 (StreamCache):** `st=40`, op4-RECVexch en **exchange 6** (sirviendo las
  peticiones de entrada del decoder), cadena `0x08171500 → 0x081f39a4 → 0x081d0a2c`.
Las TRES en rendezvous inter-tarea; **NINGUNA polleando hardware**; gate primado;
`copy32=0`. Snapshot **post-watchdog**: t2a y t25 pasan a `st=00` (**desmontadas**)
y los buzones 9/0x52 a cero — el teardown del watchdog. Es **exactamente** el
deadlock de §11.12/§11.13, ahora confirmado EN la propia tarea decodificadora.

**D) El decode SÍ hace trabajo de códec, pero NUNCA escribe PCM a su buffer de salida — VERIFICADO (`runtrace.sh` range + §11.9).**
`range=0x08080000-0x080b0000` disparado en setup + watchdog: **672 bloques
distintos de códec** corren (región `0x080axxxx`: bring-up del motor + `ParseASC
0x080a3cd0`, config). Pero la fuente del copy `0x08889098` (región
`0x08888000-0x0888c000`) sigue recibiendo **sólo el memset** (§11.9/§11.15) y
`copy32=0`. El motor rinde su bring-up y **su copy SALTA** (`[descr+5]=0`,
"PCM no listo"). El decode-a-PCM por-trama (que escribiría `0x08889098` y pondría
la bandera de trama-lista) **nunca se dispara**, porque está gateado tras el
viaje-redondo de render que está en deadlock.

**E) Determinación de ARREGLO DE MODELO: NINGUNO — cerrado por medida.**
El elemento que falta es **el comando de render en frío (buzón 0x52) al motor
t25**. Es un **mensaje inter-tarea**, no un evento de HW: las tres esperas del lazo
son rendezvous (buzón 9 / buzón 0x52 / exchange 6), ninguna es un poll de
periférico (confirmado VIVO en §C; coherente con `-d unimp`=sólo 43 writes GPIO
§11.11-B, IRQ16+tick vivos §11.12-D, salida idéntica a Rockbox §11.13-C). Por
tanto **ningún periférico, timer, valor de HW ni estado inicial puede satisfacer
esa espera** — coherente con las 8 sesiones. La carrera regime-A/B es
timing-sensible pero **el desenlace es invariante** (`copy32=0` en A y B), así que
tampoco un cambio de timing la desbloquea.

**F) La SALIDA WAV en régimen A lo confirma de forma independiente — es SÓLO el
clic de UI, cero música (`runwav.sh`, `AUDIOWAV`).** Captura de 72.6 s con decode
confirmado (`h12=1` a `+15.17 s` tras el watchdog): **`nonzero=0.25 %` (99.75 %
silencio), `L==R=100 %` (fake-mono, `rms(L-R)=0.0`), 13 ráfagas IDÉNTICAS de 624
muestras con pico 11444** (una por pulsación de SELECT en la navegación). Música
real sería `nonzero≈100 %`, `L==R≪100 %`, `rms(L-R)≫0`. No hay contenido con que
correlacionar el AAC. (Higiene: el fichero `~/6g/EVIDENCE_realaudio.wav` de una
sesión previa mide EXACTAMENTE esa firma —11444/624/L==R—, así que es **el clic mal
etiquetado como "realaudio"**, no prueba de audio. El discriminador fiable es
`copy32>0`/`memw` del decoder, no la amplitud del WAV.)

**VEREDICTO (medido, honesto): el retailOS NO suena. Deadlock de bootstrap
circular INTRÍNSECO del pipeline MeCCA, sin arreglo de modelo.** El productor
(t2a) y el consumidor (t25) se bloquean mutuamente en frío: el motor rinde PCM
⟸ comando de render (buzón 0x52) ⟸ la salida consumió PCM real ⟸ el motor rinde;
y el decoder produce la trama 0 ⟸ viaje-redondo de render ⟸ el motor rinde. Ninguna
mitad está cebada. La entrada está sana (ex6=57–87, t20 sirviendo), el decoder
primado (gate=0x4), y aun así `copy32=0` de punta a punta. El watchdog de 15 s del
propio firmware es la prueba de que el arranque en frío no cierra.

**Correcciones al registro que aporta esta sesión (todas VERIFICADAS):**
1. `obj+0x54` = **watchdog de 15 s** (desensamblado de la rueda), no un canal de
   decode → **la "tarea AAC async" del §11.15 es un fantasma**; el frente de este
   workflow descansaba en una misidentificación.
2. `h12=1` **es alcanzable** (§11.15 acertó) pero **sólo vía el watchdog de 15 s**;
   `h12=0` de Report-1 era medir antes del vencimiento. `copy32=0` invariante.
3. **Primer examen vivo de t2a** (el decoder real): bloqueado en la entrada
   (buzón 9), nunca decodifica; el muro está en el **disparo de SALIDA** (buzón
   0x52 al motor), no en `obj+0x54`.

**La vía real (no es de modelo, documentada):** un **HLE del cebado en frío** que
emita el **primer comando de render (buzón 0x52) al motor t25** para arrancar al
consumidor —que tirará de la trama (render-pull tipo-12) y hará que el decoder del
HUÉSPED produzca el PCM—. Es MÁS que el shim tipo-12 de §11.15 (que ahora se
demuestra insuficiente: h12 sólo consume el gate y **arma el watchdog**, no
decodifica): hay que cebar el LADO DEL MOTOR, y la vinculación del buffer de salida
(descriptor `0x0bb12d54`, bandera `[descr+5]`) debe salir del render propio del
huésped. Alternativa: `icount` (QEMU 9.x) para reordenar el scheduling y explorar
si el orden concurrente real difiere — con la salvedad del auditor de que el
determinismo por sí solo no hace aparecer al emisor ausente del buzón 0x52.

**Higiene:** cero cambios en fuentes del modelo; `git diff` vacío; build
byte-idéntico. Sólo lecturas gdb no invasivas + `pctrace` (ya en árbol).
Rockbox/navtest/5G sin tocar. Recetas de sesión en `~/dbg/` (`runmeas.sh`,
`runtrace.sh`, `runt2aexam.sh`, `t2a_examine.py`), fuera del repo.

### 11.17 El shim del TIMBRE de render (`HLE_RENDER_KICK`): IMPLEMENTADO y su MECANISMO VALIDADO, pero el motor es un CONSUMIDOR guiado-por-trabajo — tocar su timbre NO produce render sin PCM decodificado (refuta la hipótesis "cebar el buzón 0x52")

Sesión de **implementación** del cebado del LADO DEL MOTOR que §11.16 dejó
especificado (y que las dos investigaciones de cierre — formato del buzón 0x52 +
la cascada a PCM — detallaron). Se construyó el disparo del **timbre de render**
al motor t25 y se midió su efecto con navegación FIABLE. VA = off + `0x07FF51B8`;
IRAM en `0x22xxxxxx`. Todo **VERIFICADO** por `pctrace` (contadores `pc=`/`sendex=`
+ `memw`), la traza del propio shim, y snapshot gdb no invasivo. Disco
`Emulti.img`, `AUDIOPACE=1`, SELECT fiable (`iremote press ret -u -w e --region
right -r 15`).

**Corrección de marco que confirma la investigación (leer primero).** El "buzón
0x52" NO es un buzón KS con payload: el motor t25 se bloquea en `recv()` sobre un
**fast-semaphore SIN payload** `0x220106d0` (el "timbre"). VERIFICADO: el wrapper
de recv de t25 `0x22006814` carga `r0=0x220106d0, r1=0xbb8` (3 s) y llama a
`0x22003868`. El engobj es `0x220106dc` (= ring+12; `[engobj+0]=vtable 0x22008980`
puesto por el ctor `0x220068e4`). La "orden de render" es: armar la puerta
`[engobj+71]=1` (el motor sale por `beq 0x8082020` si es 0, `0x808171c` `ldrb
r0,[engobj+71]`) + tocar el timbre (V() del fast-sema). El emisor propio del
firmware es el stub IRAM `0x2200683c` (arma `[engobj+71]=1`, limpia `[+68]/[+70]/
[+72]`, incrementa `0x22008c1c`, señala) — pero su precondición `[engobj+70]!=0`
está **falsa en frío** (`bxeq lr` @`0x2200684c`) y tiene **0 llamadores**: por eso
NADIE toca el timbre en frío. (Confirma §11.16-C: t25 esperando el disparo de
salida.)

**A) Lo implementado (GATEADO, build por defecto byte-idéntico).**
`hw/arm/ipod_classic/ipod_classic_hle_audio.c` (+`.h`), cableado en la máquina y
disparado por el bit *run* del I2S. Con `HLE_RENDER_KICK` sin poner **no instala
nada** (init retorna antes de tocar estado; el hook del I2S es no-op). Con
`HLE_RENDER_KICK=1`: un timer de reloj virtual, cuando t25 está encolado en el
timbre (`[0x220106d0+4]!=0`, waiter presente) **y** la CPU está en el idle loop
RTXC (`pc∈[0x22003b0c,0x22003b34)` o `halted` en WFI), redirige el contexto idle
a: (1) escribir `[0x220106dc+71]=1` (la puerta de render, como el emisor propio),
y (2) llamar al **stub propio del huésped `0x22006870`** (`r0=0x220106d0; r1=0; b
0x22003670→V() 0x080a1858`), con LR de vuelta al idle. Cero PCM inyectado; se
toca el timbre EXACTO que el HW tocaría, y el desencolar+despertar los hace el
propio KS del huésped bajo sus locks. Reutiliza el mecanismo de entrega-desde-idle
validado en §11.15 (commit `dd068aa1e`), apuntando al MOTOR en vez del decoder.

**B) EL MECANISMO FUNCIONA — VERIFICADO.** Traza del shim: "rang render doorbell
#N to engine t25 (ring=0x220106d0 waiter=0x0bb1306c [engobj+71]=1) from idle
pc=0x22003b20". El timbre se toca desde el idle (pc=`0x22003b20`, exactamente el
pc del snapshot de §11.16), **9 791–11 244 veces** por corrida, despertando el
recv de t25 (waiter `0x0bb1306c` = nodo en la pila de t25, el mismo de la
investigación). Ninguna tarea viva se perturba (`resets`=1–2 = el watchdog de 15 s
del propio retailOS, idéntico a baseline). El snapshot vivo confirma con el shim
activo: `[engobj+71]=01` (armado por el shim), t25 `state=40` parkeada en el timbre
(RET `…0805c558 080b3e80 22006824 22002f84`), decoder primado (`gate[ctx+0x60]=
0x4`).

**C) HALLAZGO DECISIVO: el timbre NO produce render — el motor es un CONSUMIDOR
guiado-por-TRABAJO, no por el timbre.** Instrumentado el camino de t25 tras el
wake (MEDIDO, corrida v2):
```
recv    @0x22006814  hits=11244     <- t25 despierta del timbre 11244 veces
recvret @0x22006824  hits=11243
dpump   @0x22006118  hits=1         <- el data-pump (render) corre UNA vez (bring-up)
dpeng   @0x22006300  hits=1  dpbail=0  dpalt=0
engine  @0x0808170c  hits=1     mixer @0x0809a74c  hits=1     copy32 @0x08016eb4  hits=0
```
El bucle de t25 es `0x22002f5c`: `bl 0x22005e48` (@`0x22002f80`, ret `0x22002f84`)
**sondea la FUENTE** — `0x22005e48` es un dispatch virtual `ldr r0,[r0]; ldr
r1,[r0]; ldr r1,[r1,#28]; bx r1` (llama al método `+28` del objeto fuente). Si la
fuente no tiene datos, devuelve 0 y t25 **vuelve al recv** (`0x22002f88 beq`). El
timbre despierta al consumidor, pero **no puede fabricar trabajo que no existe**:
la fuente (PCM decodificado) está vacía, así que t25 hace wake→sondea→0→re-park,
11 244 veces, **sin renderizar ni una trama más que el baseline**. `copy32=0`
invariante; `memw=0x08888000-0x0888c000` = **8192 stores, TODOS memset**
(`0x22000198`/`0x220001a0`), CERO del decoder. Idéntico a §11.16.

**D) El engobj está SEMI-configurado — VERIFICADO (snapshot).** `[engobj+88]=0xff`
(el TIPO de render sigue en el valor de init del ctor `0x220068e4`; el `switch` del
data-pump `0x220062a0` y del motor operan sobre él — nunca se configuró a un tipo
real). `[engobj+70]=0` (la puerta de arranque en frío del emisor propio, nunca
satisfecha). `[engobj+68]=0`. El motor nunca completó su configuración de render;
el timbre por sí solo no la completa.

**E) EL MODELO CAUSAL CORREGIDO (refuta la hipótesis de la tarea y de 8 sesiones).**
La cadena que la orden de trabajo asumió —"el motor renderiza → emite el tipo-12
al decoder → el decoder produce PCM"— es **FALSA y ahora está REFUTADA por medida**:
el render es el lado **CONSUMIDOR** (`motor 0x0808170c → mixer 0x0809a74c →
copy 0x08016eb4 → ping-pong`), NO emite el tipo-12 al decoder (ver la
investigación "cascada"); el tipo-12 al decoder va por vía separada. Cebar el
timbre del motor **arranca al consumidor, pero el consumidor no tiene nada que
consumir** porque el PRODUCTOR (decoder t2a) está bloqueado en su ENTRADA (buzón
9, RECVmbox dentro de `dstep 0x08123284`) y nunca produce PCM (§11.16-C). Es el
**deadlock de bootstrap circular** de §11.16, ahora confirmado DINÁMICAMENTE desde
el lado del motor: despertar al consumidor 11 244 veces no genera ni un render
extra.

**VEREDICTO (medido, honesto): el retailOS NO suena.** El timbre de render (buzón
0x52) es **necesario pero INSUFICIENTE**. Hasta dónde llegó el kick, con evidencia:
1. **¿El motor renderizó?** NO más que el baseline: `engine=1` (sólo el bring-up
   del watchdog), pese a 11 244 toques del timbre. El motor despierta pero su
   bucle sondea la fuente, la halla vacía y re-parkea.
2. **¿El decoder decodeó?** NO: `h12=1` sólo tras el watchdog (como baseline),
   `copy32=0`, `memw`=sólo memset. t2a sigue bloqueado en la entrada (buzón 9).
3. **¿`copy32` subió de 0?** NO: `copy32=0` invariante (A y B). El WAV capturado
   es SÓLO el clic de UI (`nonzero=0.26 %`, `L==R` 100 %, pico 11444) — no música,
   nada con que correlacionar el AAC.

**Lo que FALTA (el muro real, un nivel más abajo del timbre):** que el PRODUCTOR
(decoder t2a) consuma su entrada (buzón 9) y decodifique la trama 0, poniendo PCM
en la fuente del copy (`0x08888xxx`). Sólo entonces el bucle de t25 (ya despertado
por el timbre) hallará trabajo y renderizará. El timbre del motor es el segundo
eslabón; el primero (entrada del decoder) sigue ausente. Coherente con el
"deadlock circular intrínseco, sin arreglo de modelo" de §11.16 (las tres esperas
son rendezvous inter-tarea, ninguna es poll de HW).

**Decisión sobre el shim: REVERTIDO (mudo), finding conservado.** El shim es
limpio, gateado y default-inerte (build por defecto byte-idéntico; navtest 6G y
Rockbox sin cambios con la env sin poner), y su **mecanismo de entrega está
validado** — pero **no produce audio** (el timbre despierta al consumidor sin
trabajo). Igual que el shim tipo-12 de §11.15 (revertido en `6e9f4a9c7`), se
revierte la fuente y se conserva SÓLO esta documentación medida. Reproducible: es
`dd068aa1e` reapuntado al timbre (`pc=0x22006870`, previo `[0x220106dc+71]=1`),
gateado por `HLE_RENDER_KICK`.

**Direcciones nuevas VERIFICADAS esta sesión (disasm + medida):**
- t25 bucle de render `0x22002f5c`; sondeo de trabajo `bl 0x22005e48` @`0x22002f80`
  (ret `0x22002f84`); `0x22005e48` = dispatch virtual `bx [[[fuente]]+28]` (sondea
  la fuente, devuelve 0 si vacía).
- recv del timbre `0x22006814` = `recv(0x220106d0, 3000ms)` vía `0x22003868`.
- data-pump `0x22006118`; llamada al motor `bl 0x22003840`(→`0x0808170c`)
  @`0x22006300`; limpia la puerta `strb #0,[engobj+71]` @`0x22006308`.
- puerta de entrada del motor: `0x8081718 bl 0x8015860` (getter engobj) →
  `0x808171c ldrb r0,[engobj+71]` → `0x8081730 beq 0x8082020` (sale si 0).
- stub de timbre `0x22006870` (`r0=0x220106d0; r1=0; b 0x22003670`→V()
  `0x080a1858`, veneer que no toca LR); emisores propios `0x220060e0` /
  `0x2200683c` (0 llamadores en frío).
- ctor del engobj `0x220068e4`: `[engobj+0]=vtable 0x22008980`, `[engobj+88]=0xff`.

**Higiene:** `HLE_RENDER_KICK` off por defecto → build byte-idéntico; shim
REVERTIDO tras medir (sólo esta doc se commitea). Rockbox/navtest 6G/5G intactos
(el shim era inerte sin la env; el árbol final es baseline). Recetas de sesión en
`~/dbg/` (`run_rk.sh`, `run_rk2.sh`, `run_rksnap.sh`, `rk_probe.py`,
`rk_probe2.py`), fuera del repo.

### 11.18 El DOBLE KICK (`HLE_DUAL_KICK`): el decode del HUÉSPED SÍ corre por primera vez, pero productor y consumidor están DESLIGADOS en frío — deadlock simétrico confirmado a nivel de instrucción, sin arreglo de kick

Sesión de **implementación** del doble kick que las dos investigaciones de cierre
(cebado del decoder + orden/timing) especificaron: cebar el DECODER (que produzca
la trama 0) **y** el MOTOR (que la consuma), a la vez / decoder-primero, para
romper el deadlock simétrico. Se construyó, se **corrigió el punto de entrega del
decoder** a nivel de instrucción, y se **midió** con navegación FIABLE. VA = off +
`0x07FF51B8`; IRAM en `0x22xxxxxx`. Todo **VERIFICADO** por desensamblado directo
(`~/6g/osos.dis`/`iram.dis`), snapshot gdb no invasivo de un play navegado fiable, y
contadores `pctrace` (`pc=`/`memw=`/`memwpc=`). Disco `Emulti.img`, `AUDIOPACE=1`,
SELECT fiable. Recetas nuevas en `~/dbg/` (`run_dk.sh`, `run_dksnap.sh`,
`run_dk_snap2.sh`, `dk_probe.py`, `dk_probe2.py`); shim en `~/dbg/hle_dual_kick_shim/`.

**Corrección VERIFICADA del punto de cebado del decoder (a nivel de instrucción).**
El dispatcher `0x08123dcc` conmuta por `[msg+4]`: **12**→watchdog `0x8123970`
(§11.16, confirmado); **19**→`blx [[msg]+20](msg)` y luego `switch 0x8123cfc(obj,
[retval+8])`; default→`0x80eaebc`. El `switch 0x8123cfc` conmuta por
`[[retval+8]+4]-0x52800000` (10 casos): **caso 1** (`0x52800001`, `0x8123d64`) →
`bl 0x81232d8` = **DECODE-de-trama**; **caso 7** (`0x52800007`, vía `0x81235d0`) →
`bl dstep 0x08123284` → `bl 0x8025ac4` (pide entrada por ex6 + **RECV mbox 9**, DONDE
BLOQUEA). El decoder vivo está procesando un caso-7 (cadena de retorno medida
`0x08025b04→0x081232a4(dstep)→0x081236c8(caso7)→0x08123da8(switch)→0x08123e20(disp)`).
El DECODE real cuelga del caso-1 `0x81232d8`: en `0x8123390` hace
`blx [[ctx]+104]`=`0x081c0a1c` (decode-method) → `0x081c1508` (frame-pull) →
`reader->vtable[16]`=`0x081e93d8` (pull) → bucle sobre `0x081e9e70` (la math del
códec por-bloque). Alcanzable sólo si `ctx=[obj+0x34]!=0`, `[obj+0x84]==0`,
`[obj+0x38]==0` — **los tres se cumplen en vivo** (snapshot: `ctx=0x0ba90c50`,
`gate[ctx+0x60]=0x4`).

**El shim (GATEADO, build por defecto byte-idéntico).** `HLE_DUAL_KICK=1`: un timer
de reloj virtual, desde el idle RTXC (`pc∈[0x22003b0c,0x22003b34)`, sin pisar tareas
vivas), **alterna decoder-primero**: (1) DECODER = llamar la caso-1 propia del huésped
`0x081232d8(r0=decoder_obj, r1=0)` — decodifica la trama **y** la publica
(`ctx->vtable[100]=0x081c12ac` + ops de mensaje); `r1=0` hace que el post-decode
`0x81235d0` vea `r7==0` y SALTE el dstep de entrada (que haría RECV mbox 9), así **no
bloquea** — VERIFICADO que decode-method/frame-pull/pull/publish no tienen KS-wait; el
lock `0x805c430` de `ctx+80` está descontendido (t2a aparcado en el verbo de entrada,
no lo tiene). (2) MOTOR = `[engobj+71]=1` + timbre `0x22006870` (§11.17). Se gatea el
motor a que el decoder ya esté construido (nunca timbrar en frío durante el boot).
Cero PCM inyectado. Knobs: `_DEC_ONLY`, `_MOTOR_ONLY`, `_CFG` (fuerza `+88=1/+70=1`),
`_DEC_MAX`, `_TRACE`.

**A) HALLAZGO POSITIVO: el DECODE del huésped SÍ CORRE — primera vez en 11 sesiones.**
Con el doble kick (decoder-primero, caso-1 decode+publish), MEDIDO:
`decmeth 0x081c0a1c = 4192`, `fpull 0x081c1508 = 4192`, `rpull 0x081e93d8 = 4193`,
**`innerdec 0x081e9e70 = 1`**. Es decir: el kick alcanza y ejecuta la **math AAC del
huésped** (la que §11.9-§11.16 midieron como "nunca corre": `range` sólo veía
`ParseASC`=config). El mecanismo de entrega decoder-primero funciona y llega al decode.

**B) PERO el decode corre UNA sola vez — la puerta de habilitación de producción está a 0.**
`innerdec=1` pese a 4192 llamadas a caso-1. El `rpull 0x081e93d8` sale temprano
(`0x81e93f8 beq`) porque su puerta **`[[reader+0x30]+0x2b8]` = 0** (snapshot baseline:
`reader=0x0ba61f50`, `[reader+4]ring=ctx`, `decode-gate[[+48]+0x2b8]=0x00000000`). Esa
puerta es el crédito "puedes producir una trama" que gobierna **StreamCache**; a 0, el
decoder no puede producir la trama 1. Es una hambruna de **slot de salida**, no de datos.

**C) copy32 = 0: la trama publicada NUNCA llega al motor — productor y consumidor DESLIGADOS.**
Aun forzando la config del motor (`_CFG`: `[engobj+88]=1`, `[engobj+70]=1`), MEDIDO:
`copy32 0x08016eb4 = 0`; el motor t25 despierta del timbre, **sondea su fuente
`srcpoll 0x22005e48 = 19×` y la halla SIEMPRE VACÍA**; `dpump 0x22006118 = 1` (sólo el
bring-up), `dpgateclr 0x22006308 = 0` (nunca cierra un ciclo de render). El descriptor
de copia del motor **`0x0bb12d54 = 5da35da3 5da35da3…` (relleno de heap SIN
inicializar; `[descr+5]=0x5d≠1`)** — la vinculación fuente↔copia que el propio render
inicializaría NO existe en frío. Y `memw 0x08888000-0x0888c000` = **sólo memset**
(`0x22000198`/`0x220001a0`), CERO escrituras del decoder a la fuente del mixer. La trama
que el decode produjo/publicó quedó en el buffer interno del reader; **no hay camino
cableado a la fuente (basura) del motor**. `CODEC_SETUP 0x22006574` corrió (`hits=1`)
pero su config de render está ausente en el atasco (`[engobj+88]=0xff`, `+71=0`, `+70=0`
= valores del ctor `0x220068e4`; espejismo post-teardown de §11.12-A/§11.13-A).

**D) El deadlock simétrico, ahora a nivel de instrucción (VERIFICADO).** Cada mitad
está gateada por la salida en régimen del OTRO:
- Productor: `decode` ⟸ puerta `[[reader+0x30]+0x2b8]!=0` ⟸ StreamCache da crédito ⟸
  buffer de salida liberado ⟸ el motor renderiza (`copy32`) ⟸ PCM en la fuente ⟸ decode.
- Consumidor: `copy32` ⟸ `[descr+5]==1` + descriptor `0x0bb12d54` inicializado ⟸ el
  render propio del motor ⟸ fuente con PCM ⟸ el decode publicó ⟸ productor habilitado.
Las tres esperas del lazo son rendezvous inter-tarea (t2a RECV mbox 9 dentro del verbo
de entrada; t25 RECV en el timbre; t20 RECV ex6), **ninguna es poll de HW** (coherente
con §11.12-D/§11.16). Cebar UN extremo desde el idle da **un paso transitorio** (1
decode; 1 wake del motor), nunca un ciclo autosostenido, porque los recursos
compartidos (crédito de producción, descriptor de salida inicializado) sólo los produce
el pipeline en marcha, no un timbre ni una llamada-a-decode.

**VEREDICTO (medido, honesto): el retailOS NO suena con el doble kick.** Hasta dónde
llegó cada lado, con evidencia:
1. **¿El decoder decodeó?** SÍ — la math AAC del huésped (`innerdec 0x081e9e70`)
   **ejecutó por primera vez** (todas las sesiones previas: 0). Pero **UNA sola trama**:
   la puerta de producción `[[reader+0x30]+0x2b8]` está a 0 (StreamCache no da crédito).
2. **¿Escribió PCM en `0x08888xxx`?** NO — la fuente del mixer recibió sólo memset; el
   decode escribió en el buffer interno del reader y la publicación quedó desligada de la
   fuente (basura) del motor.
3. **¿`copy32` subió de 0?** NO — invariante. El motor sondea su fuente vacía (19×) y no
   renderiza; su descriptor de copia `0x0bb12d54` está sin inicializar. Forzar `+88/+70`
   no ayuda (el problema es la VINCULACIÓN de la fuente, no el tipo).
4. **¿Autosostenido?** NO. WAV = clic de UI (`nonzero=0.22 %`, `L==R`, pico 11444;
   §11.16-F), nada que correlacionar con el AAC.

**Es el VEREDICTO ESTRUCTURAL firme:** el doble kick avanza el estado **más que las 10
sesiones previas** (el decode del huésped corre), pero el deadlock de bootstrap
**simétrico** no se abre: las puertas de cebado de cada mitad
(`[[reader+0x30]+0x2b8]` en el productor; `[descr+5]` + descriptor `0x0bb12d54` sin
init en el consumidor) las alimenta el estado-en-régimen de la OTRA mitad, y ningún kick
(timbre/decode-call/config) desde el idle las fabrica. Lo único que quedaría es un **HLE
COMPLETO del camino de datos** decoder↔motor: sintetizar el flujo de tramas y los
créditos/descriptores de buffer — lo que es **inyectar PCM/falsear estado**, prohibido
por las reglas (el PCM debe producirlo el decoder del huésped y fluir por su propio
protocolo). No hay palanca de kick, ni valor de HW, ni orden/timing que cierre el lazo.

**Direcciones nuevas VERIFICADAS esta sesión (disasm + snapshot vivo):**
- dispatcher `0x08123dcc` (tipo-12→watchdog `0x8123970`; tipo-19→`blx [[msg]+20]` +
  `switch 0x8123cfc`); `switch 0x8123cfc` conmuta `[[retval+8]+4]-0x52800000`: **caso 1
  → `0x81232d8` (decode)**, caso 7 → `0x81235d0` → dstep.
- decode-de-trama caso-1 `0x081232d8` (gate: `ctx=[obj+0x34]!=0`, `[obj+0x84]==0`,
  `[obj+0x38]==0`); decode-method `[[ctx]+104]=0x081c0a1c`; frame-pull `0x081c1508`
  (`reader=[ctx+0x48]`, pull=`reader->vtable[16]`); reader-pull `0x081e93d8` (puerta
  `[[reader+0x30]+0x2b8]`, ring=`[reader+4]`); **inner-decode/math AAC `0x081e9e70`**;
  publish `ctx->vtable[100]=0x081c12ac` (no bloqueante).
- verbo de entrada `0x8025ac4`: SELECT-mbox9 `0x8015730→0x22003f7c`, SEND-ex6
  `0x8015740→0x22004138`, **RECV-mbox9 `0x8015738→0x22004260`** (bloqueo). El verbo NO
  lee la respuesta: el RECV es puro handshake.
- snapshot vivo (atasco, baseline): `decoder_obj=0x0ba9a8a4` vt=`0x088d1948`
  ctx=`0x0ba90c50` vt=`0x088daa4c`; reader vt=`0x088de828`; **`decode-gate[[reader+0x30]
  +0x2b8]=0`**; **descriptor motor `0x0bb12d54=0x5da35da3` (sin init)**; `engobj+88=0xff`.
- motor: `srcpoll 0x22005e48` (`bx [[[src]]+28]`), `dpump 0x22006118`, `dpgateclr
  0x22006308`, `CODEC_SETUP 0x22006574` (corre `hits=1`, config torn-down en el atasco).

**Correcciones al registro (VERIFICADAS):**
1. **REFINA "cebar-decoder" (report decoder-in):** el decode NO es inalcanzable — la
   caso-1 `0x81232d8` desde el idle **sí ejecuta la math AAC del huésped** (`innerdec=1`).
   Lo que falta no es el disparo sino el **crédito de producción** (`[[reader+0x30]
   +0x2b8]=0`) que gobierna StreamCache.
2. **REFINA "orden/timing" (report orden):** el motor NO sólo necesita el timbre; su
   **fuente/descriptor `0x0bb12d54` está SIN INICIALIZAR** (no sólo `+88=0xff`), y forzar
   `+88=1/+70=1` no rinde (`copy32=0`). La vinculación productor→consumidor no existe en
   frío; no es un campo que forzar sino un binding de render que sólo el render crea.
3. **CODEC_SETUP `0x22006574` SÍ corre** (`hits=1`), refinando la duda de §11.17-D: su
   config de render se ve `0xff`/`0` en el atasco por **teardown**, no por no-ejecución.

**Decisión sobre el shim: REVERTIDO (mudo), finding conservado (regla §11.17).** El shim
es limpio, gateado y default-inerte (build por defecto byte-idéntico; navtest 6G/5G y
Rockbox sin cambios con la env sin poner), y su **mecanismo de entrega del doble kick
está validado** (decoder-primero desde el idle, decode+publish no bloqueante) — pero **no
produce audio** (el lazo simétrico no cierra). Se revierte la fuente y se conserva SÓLO
esta documentación medida. Reproducible: shim en `~/dbg/hle_dual_kick_shim/`, gateado por
`HLE_DUAL_KICK`.

**Higiene:** `HLE_DUAL_KICK`/`HLE_RENDER_KICK` off por defecto → build byte-idéntico;
shim REVERTIDO tras medir (sólo esta doc se commitea). Rockbox/navtest 6G/5G intactos
(el árbol final es baseline; navtest 6G y 5G corridos de verdad al cierre). Recetas de
sesión en `~/dbg/` (`run_dk.sh`, `run_dksnap.sh`, `dk_probe.py`, `dk_probe2.py`) y shim
en `~/dbg/hle_dual_kick_shim/`, fuera del repo.

### 11.19 El HLE del CAMINO DE DATOS (`HLE_AUDIO_DATAPATH`): el retailOS saca SU canción real por su propio I2S — decode host-side, como el VideoCore del 5G

Sesión de IMPLEMENTACIÓN de la vía que §11.18 dejó especificada. Como el pipeline
de decode MeCCA está en el deadlock de arranque simétrico cerrado por medida en
§11.12–11.18 (no se auto-ceba bajo emulación fiel), se decodifica **en el host** la
MISMA canción que el huésped lee de SU disco y se entrega el PCM al buffer que el
DMA/I2S PROPIO del huésped transporta al códec — exactamente el patrón HLE del
VideoCore del 5G (`hw/arm/ipod_video/ipod_video_vc.c`). Gateado por
`HLE_AUDIO_DATAPATH`; build por defecto byte-idéntico (Rockbox y navtest intactos).

**El shim (`hw/arm/ipod_classic/ipod_classic_hle_audio.c`, gateado).**
1. **Parsea la FAT del disco del huésped** (MBR → FAT32) y enumera los ficheros de
   audio. IDENTIFICA la pista por las PROPIAS lecturas ATA del huésped: el read
   DMA que cae en el rango de datos de un fichero (hook en `ipod_classic_ata.c`
   vía `ide_get_sector`). Así se decodifica la MISMA canción que el huésped abrió.
2. **Decodifica esa pista en el host con ffmpeg** → PCM 16-bit estéreo 44100
   (único paso host-side; lee la MISMA imagen cruda del `-drive`; corre en un hilo
   aparte para no congelar la vCPU).
3. **Alimenta el PCM al canal 1 de DMAC0** (el que drena la ping-pong a la FIFO
   del I2S): en `i2s_drain`, antes de cada ráfaga paced (`pl080_pace_request`),
   escribe el siguiente trozo en `ch->src`; el cursor de lectura avanza SÓLO por
   lo que la ráfaga consumió (`moved`=Δtx_samples), no por los ticks de sondeo —
   así la canción sigue el reloj real (fill/commit).

**Correcciones VERIFICADAS a la investigación previa (importantes).**
- **La canción del huésped NO es AAC.** Los 6 M4A/AAC que la investigación de
  ENTRADA carveó (offsets crudos 8.5–55.7 MB) están **FUERA de la partición FAT** y
  NO son lo que el huésped reproduce. La biblioteca real vive en la FAT32 (empieza
  en el byte **92962816**), en `/iPod_Control/Music/Fxx/`: **4 MP3** (48 kHz,
  `libgpodNNN.mp3`) **byte-idénticos** entre sí = **una sola canción**
  ("Numb / Encore"). El huésped saca a 44100 (CLKDIV 272) → se decodifica a 44100.
- **Disco de sectores lógicos de 4096 B (4Kn).** Las LBA del MBR van en unidades de
  4096 (part1 LBA 22696 × 4096 = 92962816 = boot sector FAT); el scanner prueba
  ×512 y ×4096 y valida la firma de boot.
- **El huésped SÍ lee los datos del MP3 al reproducir de verdad** (regime A), pero
  su reproducción es NO DETERMINISTA (§11.11 A/B): a veces lee el fichero (→ ID por
  lectura), a veces se atasca antes (regime B). Para robustez, un **fallback**
  decodifica la ÚNICA canción del disco (cuando la biblioteca es inequívoca: los 4
  ficheros idénticos) al detectar el arranque de reproducción — por el edge de
  CLKCON (reconfig de reloj del play, que el boot no hace) o por una lectura del
  área de datos FAT una vez pasado el boot (I2S ha sonado >~34 s, así que es el
  usuario abriendo un fichero, no el escaneo de arranque).

**Medida (navegación FIABLE, `AUDIOPACE=1`, `Emulti.img`).**
- ID + decode: p.ej. `identified guest track N (file stream, disk offset ...)`;
  `decoded track N -> 36400496 PCM bytes (206.4 s @ 44100 stereo) -- feeding`.
- **El feed es CONTIGUO y al ritmo real** (traza de drenado VERIFICADA): src alterna
  la ping-pong `0x22008c5c`/`0x22009c5c`, `count=moved=2048` por ráfaga, cursor
  `0→4096→8192→…` monótono.
- **Correlación WAV (capturado de la FIFO del I2S del huésped) vs el MP3 del
  disco: Pearson=1.000** en régimen, con el offset avanzando 1:1 con el tiempo
  (`wav+2s→song 1.62s`, `wav+4s→song 3.62s`); pico WAV 30420 ≈ pico del MP3 30653;
  estéreo real (RMS(L−R)≫0). Es la MISMA canción, contigua, en tono. Ruido de
  fondo del correlador ~0.02–0.05 (silencio del baseline). ⇒ **SUENA la canción
  real del retailOS, por su propio DMA→I2S→CS42L55.**

**El obstáculo exacto que QUEDA (honesto).** El motor de render del huésped sigue
en el deadlock: `copy32 (0x08016eb4)=0`; el motor `0x0808170c` corre UNA vez a los
~15 s (watchdog §11.16) y no renderiza. Consecuencias:
1. **La pantalla Now Playing NO avanza / no aparece:** la UI del huésped no
   transiciona porque su pipeline de reproducción está atascado; el shim entrega el
   audio AGUAS ABAJO de ese motor. El shim rompe el silencio, no el deadlock.
2. **Watchdog de reinicio:** sin más, el retailOS reinicia (`SWRCON=0xaa5`, escrito
   en `0x08322b24`, luego spin de nops en `0x08322b28`) ~15 s tras `StartPlayback`
   (el baseline sin HLE también: es el deadlock del huésped). Corta la canción.
   - **`HLE_AUDIO_SUSTAIN` (opt-in):** el modelo de reloj suprime SÓLO ese
     reinicio-watchdog (tag en blanco "----") mientras el HLE alimenta audio → la
     canción suena ENTERA por el DMA/I2S del huésped. Coste honesto: la CPU del
     huésped queda congelada en el spin `0x08322b28` (su intento de reproducir
     falló en el deadlock); el audio continúa porque el DMA es HW y el fill corre
     en el timer del host. Sin el flag, el huésped sigue su ciclo normal (suena
     ~15 s y su watchdog reinicia).

Cerrar el punto 1 (UI + que el motor del huésped renderice él mismo) es el muro de
§11.12–11.18 (cebar el pipeline MeCCA para `copy32>0`), no abierto aquí.

**Honestidad.** El PCM es la canción REAL del disco del huésped (correlaciona con
el mismo MP3 decodificado aparte); sale por el I2S PROPIO del huésped
(DMAC0 ch1 → FIFO 0x3ca00010 → CS42L55), no por un bypass del sumidero. Lo ÚNICO
host-side es DÓNDE se decodifica el audio comprimido — un HLE del camino de datos.
El huésped conduce la apertura del fichero y el arranque de reproducción; el shim
suple el PCM que su decoder atascado no produce, y (opcional) evita que su watchdog
de deadlock se auto-reinicie. La UI de Now Playing NO avanza (obstáculo anotado).

**Higiene / gating.** Todo bajo `HLE_AUDIO_DATAPATH` (`_TRACE`, `_TRACK`,
`_SUSTAIN`, `_DISK`); sin la env el shim no instala nada y la máquina es
byte-idéntica. Hooks: `ipod_classic.c` (init), `ipod_classic_i2s.c` (fill+commit,
edge de reloj), `ipod_classic_ata.c` (offset de lectura para ID),
`ipod_classic_clk.c` (supresión gateada del reinicio-watchdog). Regresiones:
navtest 6G y Rockbox (rbmusic) con la env sin poner — sin cambios.

### 11.20 Asalto nº12 (HLE_NP): plataforma propia; decode kickeado desde idle NO entrega trama; el muro es el CONTEXTO+ENTRADA del decoder, no dos campos

Sesión con módulo propio integrado `hw/arm/ipod_classic/ipod_classic_hle_np.c`
(gateado por `HLE_NP`, build por defecto byte-idéntico), rama `dsp-deadlock-break`.
Objetivo autorizado por el usuario: cebar los bindings de control (no PCM) para
que el motor del huésped renderice (`copy32>0`) y Now Playing avance. Todo
**VERIFICADO** por `pctrace`/`memw`/gdb no invasivo + disasm (`~/6g/osos.dis`,
VA = off + 0x07FF51B8). Disco `Emulti.img`, `AUDIOPACE=1`, SELECT fiable.

**A) La función copy 0x08016e44 decodificada (fuente y gate exactos).**
`src = [r5+12] + (([r4+4]-r6-[r5])<<2)`; `gate = [r7+5]==1`. Captura VIVA de sus
args en el render de bring-up: `r5=0x088890c0` → `[r5+12]=0x08889098` (buffer
fuente); `r7=0x0bb12eb4` → `[r7+5]=0` (gate cerrado); `r4=0x0bb12d54`. El copy
salta por gate=0 / fuente vacía. Fuente `0x08889098` = buffer ESTÁTICO en la
sección de datos del osos (< 0x08976f7c), no heap.

**B) El publish 0x081c12ac es POR MENSAJE, no por campo (clave, corrige el marco
de kicks previo).** Llama a `reader.vtable[3]` (get-frame) y, si devuelve !=0,
postea la trama downstream (`bl 0x832d9dc`); si 0, no postea. **NO escribe
`[r5+12]`/`[r7+5]` del motor.** Por eso tocar el timbre (§11.17) o el tipo-12
(§11.15) nunca cablea el binding: éste llega por el MENSAJE que el motor debe
recibir y procesar, no por el timbre.

**C) El crédito de producción = `[[reader+0x30]+0x2b8]` (fn 0x81c0fd4, dos instr:
`ldr r1,[r0+0x30]; ldr r0,[r1+0x2b8]`).** Snapshot vivo: reader=0x095562c8,
credit=0x02000000 (no-cero). El reader-pull 0x081e93d8 sólo hace `cmp r0,#0; beq`
sobre él.

**D) MEDIDO: el kick-desde-idle NO entrega trama.** Con `HLE_NP` disparando la
caso-1 `0x081232d8(decoder_obj,0)` desde el idle RTXC en régimen A (decoder
primado, `gate[ctx+0x60]=0x4`): breakpoint en el retorno de la get-frame del
publish (`0x081c12e8`) → **`r0=0` (sin trama)**. `memw` de la fuente estática
`0x08888000-0x0888c000` durante el play con el kick activo: **cero escritores no-
memset** (sólo `0x22000198/a0`). `copy32=0` invariante; WAV = sólo el clic de UI
(nonzero 0.3%, L==R 100%, pico 11444). El "innerdec 0x081e9e70" que §11.18 tomó
por "math AAC" es en realidad un **gestor de cola de trabajo** (0x8c bytes) —
correr él no implica producir PCM.

**E) VEREDICTO (medido, honesto): el kick-desde-idle es insuficiente por diseño.**
El decode real requiere el **contexto de la tarea t2a** (pila/estado) y su
**flujo de entrada** (RECV mbox 9 ← StreamCache), ambos en el deadlock; ejecutar
el código de decode desde el idle corre las instrucciones pero **no produce una
trama entregable** por el protocolo de mensajes. Cebar el gate del copy sin PCM
en la fuente no rinde. La única vía nativa restante es un **HLE COMPLETO del
decode** (sintetizar entrada+contexto+encolado por el protocolo MeCCA), de
tamaño equivalente a reimplementar la integración del códec — coherente con la
decisión de §11.19 de decodificar host-side aguas abajo.

**Aportes reutilizables (rama `dsp-deadlock-break`, commit del módulo):** copy
decodificado (A), publish=mensaje (B), credit fn (C), y una plataforma gateada
`HLE_NP` que dispara el decode del huésped a voluntad para futuros experimentos.
Recetas en `~/dbg/`: `run_np.sh`, `run_npsnap.sh`, `run_npmemw.sh`, `run_pub.sh`,
`np_snap.py`, `catch_copy.gdb`, `catch_publish.gdb`.
