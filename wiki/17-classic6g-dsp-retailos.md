# El DSP de audio del retailOS: qué sabemos para emularlo (CalmADM2E)

Documento de partida para el trabajo futuro de **hacer que el retailOS de Apple
suene** en el emulador. Reúne lo que ya sabemos del camino de audio del retailOS
y del DSP que decodifica, para no empezar de cero.

Estado hoy: **Rockbox ya reproduce audio** (decodifica en el ARM; ver
[16](16-classic6g-rockbox.md)). El **retailOS no**, porque descarga la
decodificación a un DSP que no está modelado. Este documento es sobre ese DSP.

Direcciones VA del firmware = offset en `osos_decrypted.bin` + `0x07FF51B8`.

---

## 1. El hallazgo: el retailOS decodifica en el DSP, no en el ARM

Medido (ver [14](14-classic6g-audio.md)): con Rockbox el PCM sale real por el
I²S; con el retailOS, el PCM capturado en el mismo punto es **silencio** (RMS 0).
La diferencia es dónde ocurre la decodificación:

- **Rockbox** decodifica el MP3 en el ARM (`mpa.codec`) y el PCM pasa por DRAM.
- **El retailOS** se lo pasa a un **DSP de audio dedicado**, el **CalmADM2E**, y
  solo mueve el resultado. El ARM nunca ve el PCM decodificado en una forma que
  podamos capturar hoy.

Evidencia de cuánto pesa el DSP en el retailOS (literales de 32 bits en el osos):

| Dirección | Qué es | Referencias en el retailOS |
|---|---|---|
| `0x3F000000` | `ADM_BASE` — registros del DSP en su área de datos | **245** |
| `0x39000000` | `ADM_SFR_BASE` — SFRs del DSP en el sistema | **213** |
| `0x38000000` | acelerador SHA-1 (otro bloque, no el DSP) | 502 |
| `0x3CA00000` | I²S — **ya modelado**, es donde el DSP entrega el audio | 2 |

Rockbox **no toca** `0x39000000`/`0x3F000000` en su ruta de audio (por eso suena
sin DSP). El retailOS los usa cientos de veces.

---

## 2. Qué es el CalmADM2E

Un **módulo decodificador de audio basado en CalmRISC**, el núcleo DSP de
Samsung. "ADM" = *Audio Decoder Module*. Tiene su propio juego de instrucciones
(CalmRISC, 16/32-bit) y ejecuta **firmware de decodificación propietario** que el
ARM le carga. Documentado —regalo enorme— en Rockbox:
`assets/rockbox/firmware/export/s5l87xx.h:64-137`, sección "04. CALMADM2E".

Hay **dos vistas** del mismo bloque:
- `ADM_BASE = 0x3F000000` — registros de 16 bits, "mapeados en el área de datos
  de memoria del Calm".
- `ADM_SFR_BASE = 0x39000000` — los mismos, como SFRs de 32 bits del sistema.

Y un reloj propio: `DSPCLKMD` en `CLK_BASE+0x38` (`s5l87xx.h:164`).

---

## 3. El mapa de registros (de Rockbox `s5l87xx.h`)

**Este es el interfaz que habría que emular.** Lo importante para HLE es que el
DSP se maneja por un **buzón (mailbox)** y unos **búferes en DRAM**.

### El buzón: comandos y datos host ↔ DSP

| Registro | `ADM_BASE+` | `ADM_SFR_BASE+` | Qué |
|---|---|---|---|
| `CONFIG0/1` | `0x00`/`0x02` | `0x00` | configuración/control |
| `COMMUN` | `0x04` | `0x04` | **control de comunicación** (el "toca timbre") |
| `DDATA0..7` | `0x06..0x14` | `0x10..0x2C` | **datos del host al DSP** (8 palabras) |
| `UDATA0..7` | `0x16..0x24` | `0x30..0x4C` | **datos del DSP al host** (8 palabras) |

El ARM escribe argumentos en `DDATAn`, da un comando por `COMMUN`, y lee la
respuesta en `UDATAn`. Ese es el canal de órdenes — el equivalente al mailbox del
VideoCore del 5G.

### Los búferes en DRAM: dónde vive el código y los datos del DSP

El ARM le dice al DSP dónde están sus regiones, **todas en la DRAM principal**:

| Registro | `ADM_BASE+` | Qué |
|---|---|---|
| `IBASE_H/L` | `0x26`/`0x28` | inicio del **área de instrucciones** del DSP (su firmware) |
| `DBASE_H/L` | `0x2A`/`0x2C` | inicio del **área de datos** del CalmRISC |
| `XBASE`,`YBASE` | `0x2E..0x34` | áreas X/Y del MAC (multiplicador-acumulador del DSP) |
| `S0BASE_H/L` | `0x36`/`0x38` | inicio del **búfer secuencial 0** |
| `S1BASE_H/L` | `0x3A`/`0x3C` | inicio del **búfer secuencial 1** |

En `ADM_SFR_BASE` los mismos como 32-bit: `ADM_IBASE` `+0x50`, `ADM_DBASE`
`+0x54`, `ADM_XBASE` `+0x58`, `ADM_YBASE` `+0x5C`, `ADM_S0BASE` `+0x60`,
`ADM_S1BASE` `+0x64`.

### Los búferes secuenciales: la clave para HLE

`SBFCON`/`SBFSTAT` (`ADM_BASE+0x42`/`0x44`) controlan dos **búferes secuenciales
en modo anillo**, con registros de offset/begin/end (`SBL0OFF`, `SBL0BEGIN`,
`SBL0END`, y los del 1, en `+0x46..0x5C`). **INFERIDO, muy probable:** son el
**anillo de entrada (audio comprimido)** y el **de salida (PCM)** — el ARM llena
uno con MP3, el DSP decodifica y llena el otro con PCM, que después el DMA lleva
al I²S. Verificarlo es el primer paso de RE.

Hay también una caché (`CACHECON`/`CACHESTAT`, `+0x3E`/`+0x40`).

---

## 4. La vía recomendada: HLE, no emular el DSP

**No** hay que ejecutar el CalmRISC ni su firmware. Se hace **High-Level
Emulation del protocolo**, igual que hicimos con el **VideoCore del BCM2722 en el
iPod Video 5G** (ver [09](09-historia-5g.md) y `hw/arm/ipod_video/ipod_video_vc.c`):
modelar *lo que el bloque hace*, no *cómo lo hace*.

Plan concreto:

1. **Modelar el bloque `0x39000000`/`0x3F000000`** como dispositivo QEMU
   (plantilla: `ipod_classic_disp.c` o el VC del 5G). Que acepte las escrituras
   de `IBASE/DBASE/S0BASE/S1BASE`, guarde esas direcciones, y responda al buzón.
2. **Interceptar el comando de decodificar** en `COMMUN`/`DDATAn`: averiguar por
   RE qué valor significa "decodifica este bloque".
3. **Leer el audio comprimido** del búfer de entrada (uno de los S0/S1, en DRAM,
   en la dirección que el ARM programó) y **decodificarlo en el host** (libmad o
   ffmpeg dentro del modelo).
4. **Escribir el PCM** en el búfer de salida (el otro S0/S1) y **avisar** (poner
   `UDATAn`/`SBFSTAT` y levantar la interrupción que el ARM espera).
5. A partir de ahí el camino ya está: el DMA lleva el PCM al I²S
   (`0x3CA00000`, **ya modelado**), el códec CS42L55 está modelado, y la captura
   a WAV / la salida a altavoces ya existen.

El firmware de Apple pide "decodifícame esto" y recibe PCM: no nota que el
CalmRISC no corrió.

---

## 5. Lo que ya está modelado aguas abajo del DSP

Todo esto **ya funciona** (se probó con Rockbox) y el DSP solo tiene que
alimentarlo:

- **I²S** en `0x3CA00000` (`ipod_classic_i2s.c`): FIFO, readback, captura a WAV.
- **Códec CS42L55** por I²C `0x4A` (`ipod_classic_cs42l55.c`).
- **DMA de audio**: DMAC0, línea `IIS0_TX`, con pacing al ritmo de muestreo
  (`hw/dma/pl080.c`, `AUDIOPACE`). `IRQ_DMAC0 = 16`.
- Frecuencia: 44100/48000 Hz según la pista (el retailOS pone `I2SCLKDIV`).

Así que el trabajo del retailOS-con-audio es **solo la caja del DSP**: desde el
PCM en el búfer de salida hacia adelante ya está hecho.

---

## 6. Lo que falta averiguar (RE, primer paso concreto)

Todo esto se saca **trazando lo que el retailOS escribe** al reproducir una
canción (instrumentar `0x39000000`/`0x3F000000` con una traza tipo `DSPTRACE`,
como se hizo con el compositor):

1. **La secuencia de arranque del DSP**: cuándo carga el firmware a `IBASE`, cómo
   lo arranca (`CONFIG0`?), y de dónde saca ese firmware (¿está en el osos? ¿lo
   descifra?). Si el firmware del DSP hay que cargarlo y no lo tenemos, el HLE lo
   ignora — esa es justo su ventaja.
2. **El protocolo del buzón**: qué comando en `COMMUN` significa decodificar, qué
   argumentos van en `DDATAn`, qué devuelve en `UDATAn`.
3. **Los búferes S0/S1**: cuál es entrada y cuál salida, su tamaño, el formato
   exacto del audio comprimido que recibe (¿frames MP3 crudos? ¿algo pre-parseado
   por el ARM?) y del PCM que produce (16-bit estéreo, ¿entrelazado?).
4. **La interrupción de "bloque decodificado"**: cuál es y cómo la espera el ARM
   (probablemente para pedir el siguiente bloque, como el doble buffer de Rockbox).

Fuente de oro para 2-4: **cómo lo hace el propio firmware**. También conviene
mirar si **freemyipod** o el research de **giek2000/ipod-classic-firmware-research**
documentan el protocolo del ADM; y si el modelo de **freemyipod/qemu**
(rama s5l8702-dev) ya toca este bloque.

---

## 7. Referencias

- `assets/rockbox/firmware/export/s5l87xx.h:64-137` — **el mapa de registros del
  CalmADM2E**, la fuente primaria de este documento.
- [14-classic6g-audio.md](14-classic6g-audio.md) — la medida de que el retailOS
  da silencio y Rockbox no.
- [16-classic6g-rockbox.md](16-classic6g-rockbox.md) — el camino de audio que
  **sí** funciona (Rockbox en el ARM), aguas abajo del DSP.
- [09-historia-5g.md](09-historia-5g.md) y `hw/arm/ipod_video/ipod_video_vc.c` —
  **el precedente**: HLE del VideoCore del BCM2722 modelando su protocolo, no su
  ISA. El patrón a repetir con el DSP.
- [02-s5l8702-hardware.md](02-s5l8702-hardware.md) — el mapa de memoria del SoC.
