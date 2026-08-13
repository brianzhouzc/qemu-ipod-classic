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
