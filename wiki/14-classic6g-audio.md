# El camino de audio del iPod classic 6G (S5L8702) en QEMU

Qué hace falta para que el retailOS **suene**, qué está modelado ahora, y dónde
está el muro que queda. Todo lo que sigue está **medido** salvo donde diga
`INFERIDO` (razonado a partir de algo verificado) o `INDETERMINADO` (no lo
sabemos y no se finge). Las trazas son reproducibles con las órdenes del final.

> Contexto: la lista de música ya funciona (Music -> Songs muestra las pistas
> con título y artista; ver [13-classic6g-musica.md](13-classic6g-musica.md)).
> Lo que faltaba es que al seleccionar una canción **suene**. Este documento es
> ese frente.

---

## 0. Estado por hito

| Hito | Estado |
|---|---|
| — Modelar el bloque I²S (antes `create_unimplemented`) | **hecho** — `ipod_classic_i2s.c`, con readback, FIFO y captura a WAV |
| — Códec CS42L55 como esclavo I²C en `0x4A` | **hecho** — `ipod_classic_cs42l55.c` |
| 1. Now Playing se dibuja entera (barra + info) | **NO alcanzado** — bloqueado antes: los botones no registran en la lista de canciones (§5) |
| 2. La posición de reproducción avanza con el tiempo | **mecanismo hecho y demostrado** en el push del arranque; el hito (barra) necesita reproducción real (§6) |
| 3. Volcar el PCM decodificado a un WAV reproducible | **infraestructura hecha; contenido = silencio**, bloqueado por el DSP (§4) |

Resumen honesto de dónde me quedo, con evidencia medida:

- **La captura a WAV funciona** (12288 bytes = 6144 muestras por arranque,
  44100 Hz 16-bit estéreo, WAV válido), pero lo que captura es **silencio**
  (RMS 0, ningún aviso "NON-ZERO"). Razón: **el retailOS decodifica el MP3 en el
  DSP CalmADM2E, no en el ARM**, y el DSP no está modelado (§4).
- **No llego a Now Playing** porque hay un muro *antes* del audio: en la **lista
  de canciones**, la rueda **gira** (mueve el resaltado) pero los **botones**
  (SELECT y MENU) **no registran** — 100+ intentos, con y sin toque de rueda
  previo, en dos discos, con varios tiempos de pulsación. MENU es navegación
  pura (nada de audio), así que el muro es de **entrada/UI**, no del camino de
  audio, y **no lo causan mis cambios** (§5).
- El **readback de I2STXCOM** que los informes proponían como llave de Now
  Playing resultó **inocuo pero no ser la llave**: el firmware hace un
  read-modify-write (`ldr; orr #6; str`, VA `0x2200876c`) que **escribe 6 pase
  lo que pase** en la lectura (§5).

El camino a "un WAV con la canción" sigue siendo **Rockbox** (decodifica en el
ARM), pero sólo hay bootloader compilado y bootearlo es un desvío grande (§7).

---

## 1. El mapa del bloque I²S — base `0x3CA00000`

Base confirmada por dos fuentes que coinciden: `I2S_MEM_BASE`
(`include/hw/arm/ipod_classic/ipod_classic.h:102`) == Rockbox `I2S_BASE`
(`assets/rockbox/firmware/export/s5l87xx.h:1182`). Hasta ahora era
`create_unimplemented_device`; ahora es un dispositivo real
(`hw/arm/ipod_classic/ipod_classic_i2s.c`).

| Off | Registro | Valor que escribe el retailOS | Marca |
|---|---|---|---|
| +0x00 | `I2SCLKCON` | `0x00000001` (clock on) | **VERIFICADO** (traza en vivo, pc~`0x22008800`) |
| +0x04 | `I2STXCON` | `0x0b100019` | **VERIFICADO** (== Rockbox `pcm-s5l8702.c:204`) |
| +0x08 | `I2STXCOM` | `0x6` = start (Rockbox usa `0xe`/`0xa`) | **VERIFICADO**; y se **lee de vuelta** |
| +0x10 | `I2STXDB0` (FIFO) | destino del DMA: muestras PCM 16-bit | **VERIFICADO** |
| +0x30 | `I2SRXCON` | `0x00001000` | **VERIFICADO** |
| +0x34 | `I2SRXCOM` | — | INFERIDO (no visto en playback) |
| +0x38 | `I2SRXDB` | — | INFERIDO |
| +0x3C | `I2SSTATUS` | — (ni leído ni escrito en mis corridas) | **INDETERMINADO** |
| +0x40 | `I2SCLKDIV` | `0x110`=272 | **VERIFICADO** (pc~`0x22008714`) |

Frecuencia y formato — **VERIFICADO**: `I2SCLKDIV = MCLK/samplerate`,
`MCLK = 12 MHz` ⇒ 272 → 44100 Hz. La rutina `set_freq` del retailOS está en VA
`0x0808e028`: tabla 8000/11025/12000/16000/22050/24000/32000/44100 con el mismo
truco de 32 kHz que Rockbox (a 48 kHz con MCLK/3), y termina en
`cscodec_write(5, code)` (`0x0808e138`) que fija `CLKCTL2` del códec. Muestra =
**16-bit** (DMA `WIDTH_16`), **estéreo** L/R intercalado.

### Lo que el modelo hace (hito 1)

Un fichero de registros con **readback honesto**. La clave: el retailOS escribe
`I2STXCOM=0x6` y lo **lee de vuelta** para confirmar que el transmisor arrancó;
mientras el bloque era `create_unimplemented` esa lectura devolvía 0. Medido con
el modelo nuevo: una lectura de `+0x08` devuelve `0x00000006` (antes, 0). El
`I2SSTATUS` (+0x3C) devuelve 0 de momento — sus bits son INDETERMINADOS y ningún
driver que tengamos lo lee; si el dibujado de Now Playing resulta gatearse en él,
saldrá en la traza `I2STRACE` durante un play real (§5).

`I2STRACE=1` traza cada acceso (menos el FIFO, que son miles) con su valor y el
PC del huésped, igual que `DISPTRACE`/`ATATRACE`/`WHEELTRACE`.

---

## 2. El transporte: DMAC0 (PL080) al FIFO del I²S

Controlador: **DMAC0** en `0x38200000` (`hw/dma/pl080.c`, IRQ 16). El canal de
playback (Rockbox `dma_play_ch_cfg`, `pcm-s5l8702.c:61-75`):

- `srcperi = MEM` (DRAM, `sinc = INC`), `dstperi = IIS0_TX = 0xA`
  (`dma-s5l8702.h:50`), destino fijo `0x3CA00010` (FIFO, `dinc = INC_DISABLE`).
- `swidth=dwidth=WIDTH_16`, LLIs encadenadas, IRQ de terminal-count por chunk.

Corroborado en el modelo: la tabla de FIFOs de periférico de la infraestructura
de DMA del retailOS vive en IRAM y contiene `0x3CA00010` (TX) y `0x3CA00038`
(RX), en `0x22008a94` (VERIFICADO por desensamblado). El único destino que DMAC0
programa hacia audio es `0x3CA00010`.

---

## 3. El códec CS42L55 (I²C0 `0x4A`)

**VERIFICADO**: es un Cirrus Logic **CS42L55**, bus I²C0, esclavo 7-bit `0x4A`
(8-bit `0x94`). Dos fuentes coinciden: Rockbox `cscodec-6g.c:39`
(`i2c_write(0, 0x94, reg, 1, &data)`) y el retailOS, cuyo `cscodec_write` está en
VA `0x0808e1ac` (carga `r0=0x4a`, longitud 2, y llama a la misma rutina I²C que
usa el PMU, `0x08325a20`). La init del códec del retailOS (VA `0x08086000`, 19
`cscodec_write`) reproduce `audiohw_preinit()` casi byte a byte, con
`CLKCTL2=0x0b` = 44100 Hz por defecto.

El modelo (`ipod_classic_cs42l55.c`) es un esclavo I²C con puntero de registro y
auto-incremento —igual que el PMU—, 128 registros, readback honesto (que es lo
que necesita el read-modify-write de los drivers), y el id de chip en `0x01`.
Antes no existía: las escrituras a `0x4A` recibían NACK y las lecturas devolvían
basura.

---

## 4. El muro del hito 3: el DSP decodifica, y no está modelado

**El ARM NO decodifica el MP3 en el retailOS.** Lo hace el DSP **CalmADM2E**
(`ADM_SFR_BASE 0x39000000` / `ADM_BASE 0x3F000000`), hoy `create_unimplemented`.
Evidencia:

- **VERIFICADO** (experimento): con `AUDIOWAV=/ruta.wav` capturé todo lo que el
  DMA vuelca al FIFO en un arranque. Resultado: **6144 muestras, todas 0** (la
  cadena arranca y clockea silencio). El WAV sale válido (44100 Hz, 16-bit
  estéreo) pero mudo.
- **INFERIDO-fuerte**: el firmware referencia el espacio del DSP cientos de veces
  (0x39000000 / 0x3F000000); Rockbox, que decodifica en el ARM, no toca el DSP.
  El buffer PCM lo llena el DSP, y sin DSP el buffer queda a cero.

Implicación: **capturar audio reconocible del retailOS exige modelar el
CalmADM2E** (un DSP programable con firmware de decode de Apple cargado) —
trabajo grande, fuera del alcance de esta fase. La alternativa realista es
Rockbox (§7).

---

## 5. Hito 1 — el muro real: los botones no registran en la lista de canciones

**No se alcanza Now Playing**, y el obstáculo está *antes* del audio. Medido con
`tools/ipod_classic/iplay.sh` (navegación + capturas) sobre `Emulti.img` (4
pistas de Linkin Park) y `E4kzero_rebuilt.img` (1 pista):

- La navegación por **menús** funciona igual que antes: idioma → menú principal
  → **Música** → submenú → **Skladby/Songs** → **lista de canciones**. SELECT
  entra en cada submenú (medido: `diff` de 6000–38000 px por transición) y la
  lista sale con sus 4 pistas, título y artista. **Cero regresión.**
- En la **lista de canciones**, la **rueda gira**: un `e` mueve el resaltado de
  "In The End" a "Numb - Encore" (nonblack 75372→75462→75515, VERIFICADO en las
  capturas `P05/P06`).
- Pero los **botones no hacen nada** ahí: **SELECT** sobre una canción no arranca
  reproducción ni dibuja Now Playing, y **MENU** tampoco vuelve atrás.
  **Medido**: 100+ pulsaciones de SELECT (holds de 60/120/250/400 ms, con y sin
  `-w` de toque de rueda previo, en ambos discos) → **0 cambios de pantalla**;
  y **0 actividad del I²S** (el contador de `I2STXCOM=6` se queda en 2, las dos
  del arranque; el WAV se queda en 6144 muestras). El discriminador decisivo:
  **MENU** desde la lista (captura `P07`) **tampoco** navega, y MENU es
  navegación pura, **sin nada de audio** — así que el muro es de **entrada/UI**,
  no del camino de audio.

Qué NO es (descartado con medida):
- **No es mi I²S ni mi códec**: la reproducción aborta *antes* de tocar el I²S
  (0 accesos nuevos), y MENU no toca audio en absoluto. Reintroducir el bloque
  no cambia esto.
- **No es el disco**: idéntico con 4 pistas y con 1 pista.
- **No es que la rueda esté muerta**: gira y mueve el resaltado en la misma
  pantalla donde el botón se ignora.
- **No es el reset de batería**: **0 resets** en todas las corridas.

Qué es: **INDETERMINADO** entre (a) el widget de lista consume los paquetes de
botón de la rueda de una forma que el modelo/arnés no reproduce, y (b) un camino
de reproducción que aborta arriba. La investigación previa (i2s-dma) sí observó
**una** vez un arranque de reproducción (2 `I2STXCOM=6` nuevos) disparando SELECT
a mano; **no lo pude reproducir** en esta fase. Es el siguiente obstáculo
concreto: **hacer que SELECT sobre una canción registre en el widget de lista**
(comparar cómo el firmware consume los paquetes de botón en un menú vs. en la
lista — es la misma rueda, distinto consumidor).

Sobre el readback que iba a ser la llave de Now Playing: el firmware **sí lee**
`I2STXCOM` (VA `0x2200876c`), pero en un read-modify-write —
`ldr r1,[TXCOM]; orr r1,#6; str r1,[TXCOM]`— que **escribe `6` pase lo que pase
en la lectura**. Así que devolver el valor escrito (mi cambio) es correcto e
inocuo, pero **no** es lo que gatea Now Playing. El `I2SSTATUS` (+0x3C) **no se
lee** en ninguna de mis corridas.

---

## 6. Hito 2 — pacing del DMA: mecanismo hecho y demostrado

**Implementado y demostrado el mecanismo**, gateado por `AUDIOPACE=1` (por
defecto **apagado**, y entonces el build es idéntico al anterior byte a byte).

Qué hace (`hw/dma/pl080.c` + `ipod_classic_i2s.c`):
- Se añade a la PL080 un campo `paced_lines` (bitmap de líneas de request). Una
  línea **no** marcada transfiere a máxima velocidad **exactamente como antes**
  — el cambio es inerte salvo que un periférico se apunte. La línea `IIS0_TX`
  (`0xA`) se marca sólo bajo `AUDIOPACE`.
- Para una línea marcada, un `mem->periférico` (flow 1) sólo avanza mientras el
  periférico ha pedido dato (`pl080_pace_request`), y el request se **consume en
  cada terminal-count**: **un chunk por pulso de request**.
- El I²S arranca un `QEMUTimer` al ver `I2STXCOM` con el bit de run, y en cada
  tick pide un chunk y **reprograma el siguiente tick según las muestras que se
  movieron** (autocorrige la cadencia).

**Medido** (arranque con `AUDIOPACE=1 I2STRACE=1`): un drenaje de **2048
muestras** programa el siguiente a **23211 µs** — es decir 1024 tramas / 44117 Hz
= 23.2 ms, **la cadencia real de 44100 Hz**. Sin pacing esas muestras entran de
golpe. **Seguro**: con `AUDIOPACE=1` la música **sigue listándose** (llego a la
lista, `diff=38030`), **0 resets**, **0 hw_error** (Apple programa el canal con
flow<4, como se temía que no; verificado por arranque limpio).

Límite honesto: sólo lo puedo demostrar sobre el **push de silencio del
arranque** (6144 muestras), porque no llego a una reproducción sostenida (§5). En
ese push, **algunos chunks se cuelan sin pacing** (sólo 1 de ~3 drena por el
camino gateado) porque no todos llevan el bit de terminal-count que consume el
request; con una reproducción real (Rockbox usa I-bit por chunk) el pacing
engancharía de forma continua. **El hito 2 de verdad —la barra que avanza—
necesita que primero se llegue a reproducir.**

---

## 7. La vía Rockbox para el hito 3

Como el transporte I²S+PL080 es idéntico al de Rockbox, arrancar Rockbox (que
decodifica MP3 en el ARM) sobre este mismo modelo produciría PCM **no-cero** por
el mismo FIFO, capturable a WAV con el mismo `AUDIOWAV` — sería el hito 3.

Estado: en `assets/rockbox/` sólo hay el **bootloader** compilado
(`build-boot/bootloader-ipod6g.ipod`, formato IM3), **no** un build completo de
la app (`rockbox.ipod`). Arrancarlo exige compilar la app entera para ipod6g,
resolver el formato IM3 (lo carga la bootrom, que no tenemos), y que Rockbox
maneje almacenamiento/LCD por SUS rutas (que tocan registros no modelados). Es un
desvío grande, fuera del alcance de esta fase, pero es **el camino recomendado**
para el audio real. El fork `assets/ref/freemyipod-qemu` (rama s5l8702-dev)
arranca Rockbox en este SoC y puede servir de referencia.

---

## 8. Cómo reproducirlo

```bash
# compilar
wsl -d Ubuntu-24.04 -e bash -c "cd /mnt/c/Desarrollo/qemu-ios/build && ninja qemu-system-arm"

# arranque con disco, capturando el PCM del FIFO a un WAV
cd build
AUDIOWAV=/tmp/audio.wav I2STRACE=1 ./qemu-system-arm -M iPod-Classic \
    -kernel ../assets/classic6g/osos_decrypted.bin \
    -drive if=ide,format=raw,file=~/6g/Emulti.img,file.locking=off \
    -snapshot -display none -serial null -monitor none

# lo mismo con el pacing del DMA activado (hito 2): las muestras entran a 44.1kHz
AUDIOPACE=1 I2STRACE=1 ./qemu-system-arm ...   # -> "I2STRACE drain: chunk N ..."

# navegar a una canción e intentar reproducir, capturando las pantallas
sh tools/ipod_classic/iplay.sh ~/6g/Emulti.img ~/6g/play.wav

# medir el WAV (RMS 0 = silencio; el DSP no está modelado, ver §4)
python3 - <<'PY'
import wave,array,math
w=wave.open('/tmp/audio.wav'); d=w.readframes(w.getnframes())
a=array.array('h'); a.frombytes(d)
print('frames',w.getnframes(),'rms',math.sqrt(sum(x*x for x in a)/len(a)))
PY
```

Variables de traza/experimento: `I2STRACE=1` (accesos I²S + cadencia de
pacing), `AUDIOWAV=<ruta>` (vuelca el PCM del FIFO a WAV), `AUDIOPACE=1`
(gatea el DMA de audio al ritmo de muestreo).
