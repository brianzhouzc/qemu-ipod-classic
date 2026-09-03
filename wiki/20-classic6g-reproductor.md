# Cómo salió el reproductor de retailOS (iPod Classic 6G)

Documento de resultado. La investigación completa, con las retractaciones, está en
`19-classic6g-dsp-hle.md` §11.110–§11.119; esto es el resumen de qué estaba roto y qué
lo arregló.

Punto de partida: el reproductor abría, mostraba media pantalla y a los 5–18 s volvía
solo a la lista. Sin sonido.

```
                ANTES                              AHORA
   +---------------------------+      Now Playing        [>] [bat]
   |  [caratula]               |      +----------+  Billie Jean
   |                           |      | caratula |  Michael Jackson
   |            (en blanco)    |      |          |  Greatest Hits
   |                           |      +----------+  1 of 4
   +---------------------------+       0:00 [==============] -4:55
     y a los 18 s, de vuelta            y sonando
        a la lista
```

## Los cuatro fallos, en orden de descubrimiento

### 1. La bomba de render se rendía a las 8 invocaciones — `SM1_SLOTS=1`

El bucle de audio pedía un búfer a `0x080a3c00` y se rendía porque devolvía 0 donde
`0x22006358` exige 2. La primera guarda de esa función lee una **máscara de
habilitación** en `0x2203ff28`, y con `memw=` sobre una ejecución entera (~180 s,
arranque incluido) **ningún PC de la CPU la escribe jamás**.

No faltaba un bit: es estado que **publica el motor SM1** en IRAM1, y nuestro modelo
era solo un banco de registros que nunca escribía memoria.

Lo que lo hizo implementable sin adivinar: **la geometría no es una constante oculta
del hardware, la publica el motor y el firmware la lee de vuelta** para derivar
dirección y tamaño (`0x080a3c54`..`0x080a3c68`). Cualquier geometría coherente vale.

| | sin | con |
|---|---|---|
| `rendret` | 8 | **1343** (~20/s, la cadencia de ranura a 44100 Hz) |
| búferes entregados | 0 | 1342 |

Dos detalles costaron dos iteraciones: liberar todas las ranuras a la vez destruye el
control de flujo (el ritmo debe salir del contador del I2S), y el aviso de trama solo
debe levantarse cuando **de verdad** se completó una, o la bomba pide, falla y
abandona a las 16.

### 2. La copia iba al revés — `SM1_DECODE=1`

El "productor" `0x081db4c8` no es un descodificador: es un repartidor de dos búferes
fijos que **devuelve éxito siempre** y entrega ceros. Y la copia de la bomba es

```
22006488: ldrne r0,[sp,#52]   ; destino = el bufer del codec
2200648c: ldrne r1,[sp,#28]   ; origen  = la ranura de IRAM1
22006494: blne 0x22000020     ; ranura -> bufer del codec
```

o sea que los búferes del códec son la **salida** de la bomba, no su entrada: el audio
tiene que estar ya en la ranura. Rellenar los búferes del códec (primer intento) era
escribir en el extremo lejano de la copia, donde la bomba lo sobrescribía acto seguido.

Descodificar MP3 como lo hace un códec no documentado no se reconstruye bit a bit: se
**emula**, y `ipod_classic_hle_audio.c` ya lo hace con salida verificada.

Comprobado con `SM1_FEED_OFF=1`, que apaga el atajo del anfitrión y deja el camino del
invitado como **única ruta posible** del PCM al altavoz:

| relleno del PCM | resultado |
|---|---|
| en el búfer del códec | 0,18 % no nulas, pico 11444 |
| **en la ranura** | **5,7 – 29 % no nulas, pico 32768** |

La ruta real es: motor → ranura IRAM1 → bomba de render → búfer del códec → driver de
salida → DMA → I2S. **Todo eso es el firmware de retailOS.**

### 3. El compositor tenía las capas al revés — `DISP_ALPHA=1`

**La pantalla de Now Playing siempre se dibujó. La tapábamos.**

Volcadas las superficies con el reproductor abierto: la capa 0 contiene el reproductor
entero —barra de título, artista, álbum, "4 of 4", barra de progreso— y la capa 1 la
carátula, opaca y a pantalla completa. El compositor pintaba de atrás a adelante
empezando por la capa 0, así que la carátula **borraba** el reproductor y dejaba
exactamente el lado derecho en blanco que parecía media pantalla.

Y la segunda mitad del mismo error: el modelo **descartaba el canal alfa**. Esa capa lo
lleva de verdad — su byte de transparencia toma **243 valores distintos**.

Componer de delante a atrás con *source-over* explica **las dos** cosas: el reproductor
queda encima de la carátula, y el barrido del menú de idioma —de donde se dedujo el
orden Z original **suponiendo capas opacas**— sigue funcionando porque el panel
entrante es transparente donde debe verse el saliente. Verificado: menús y listas se
dibujan igual que antes.

### 4. "Low Battery — Connect to Power" — `PMUCHARGER=adapter`

El aparato mostraba esa pantalla y se apagaba. `ipod_classic_pmu.c` lo daba por no
resuelto ("subir el ADC a fondo NO lo evita").

Con `PMUTRACE=1` se ve que el ADC **se lee bien** (`0x55 = 0xd3` = los 844 de 1023 que
publicamos), así que la tensión nunca fue el problema: `MBCS1_HEALTHY = 0x00` describe
"a batería, sin cargador", y el firmware pedía justamente que lo enchufaran.

**Cuidado con cuál se reporta**, y equivocarse está medido: con el **adaptador de
pared** el aparato entra en su pantalla de carga a pantalla completa y deja de
reproducir — que es lo que hace un iPod real, o sea que el modelo acertó y la elección
fue mala. Con **USB** sigue usable, pero el reproductor vuelve a abortar. Hoy el
`adapter` es el que sostiene la reproducción, y la pantalla de carga aparece solo al
quedarse ocioso: se sale pulsando cualquier tecla.

## Cómo levantarlo

```sh
IRAMSPILL=0x10000 SHA1_ZERO=1 SM1_IRQ=1 \
SM1_SLOTS=1 SM1_NSLOTS=2 SM1_DECODE=1 SM1_FEED_OFF=1 \
DISP_ALPHA=1 PMUCHARGER=adapter \
HLE_AUDIO_DATAPATH=1 HLE_AUDIO_SUSTAIN=1 HLE_AUDIO_DISK=<imagen> \
qemu-system-arm -M iPod-Classic ... -display sdl
```

`SM1_NSLOTS=2` no es cosmético: con las 7 ranuras por defecto se escriben 28 KiB desde
`0x22038100` y eso **pisa memoria ajena**, congelando la interfaz a medio entrar
mientras el audio sigue sonando. Publicar el estado es inofensivo; escribir 28 KiB no.

Teclas: `q`/`e` rueda · `Enter` centro · `w` menú · `a`/`d` pista · `s` play-pausa.
El firmware descarta ~5 de cada 7 pulsaciones del centro, así que hay que insistir --
una sola tirada perdida se lee como "el reproductor no abre" y ya costó varias pasadas.

### Sonido en el anfitrión

El I2S del modelo solo sabe escribir WAV; no tiene salida de audio propia. Para oírlo
en vivo se encamina el fichero a `ffplay`, y **hace falta `SDL_AUDIODRIVER=pulse`**:
con el controlador por defecto ffplay **se cuelga sin imprimir nada** (medido: timeout
de 20 s sobre un fichero de 3 s). Eso, y no la falta de audio, es lo que hacía parecer
que el emulador no sonaba.

## Lo que sigue sin funcionar

- **El tiempo transcurrido se queda en 0:00.** Hacer que el registro de posición del
  SM1 siga al I2S (como ya hace `sm1_stream_pos`) **no** lo mueve, así que ese contador
  lo lleva la contabilidad interna del reproductor, no el registro.
- **Pausa y cambio de pista.** La pausa corta el audio limpiamente, pero no reanuda; al
  cambiar de canción suena un chorro breve y se calla. El cursor de PCM de
  `sm1_fill_slot` es una estática que nunca se reinicia, así que al cambiar de pista
  lee fuera de sitio.
- **Todo va tras variable de entorno.** La compilación por defecto queda byte a byte
  como estaba, porque cada uno de estos cambios tiene contrapartidas medidas y ninguno
  está lo bastante cerrado como para imponerlo.
