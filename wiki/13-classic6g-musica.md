# Meterle música al Classic 6G emulado

Escrito el 2026-08-11, después de que el retailOS arrancara, dibujara y se
navegara. La pregunta siguiente era la obvia: *¿y si le metemos una canción?*

Lo que sigue está **VERIFICADO** salvo donde diga otra cosa. La parte que
quedó sin comprobar está en el apartado 5, y es la que decide si esto sirve.

## 1. Por qué no basta con copiar el MP3

El iPod no recorre el disco buscando ficheros de audio. Lee **su base de
datos**, el `iTunesDB` de `iPod_Control/iTunes/`, y enseña lo que ahí ponga.
Un disco lleno de MP3 sin base de datos da una biblioteca vacía.

Y desde esta generación —los modelos de 2007, que incluyen el classic 6G y el
nano 3G— Apple **firma esa base de datos** con un hash que depende del
identificador FireWire del aparato. Es lo que durante años impidió que las
herramientas de terceros escribieran música en estos modelos. `libgpod` acabó
implementándolo, y es la única implementación libre que lo hace.

## 2. Los datos del aparato salen de la NOR

`assets/classic6g/bootflash_nor_1MB.bin` empieza con el bloque `SysCfg`, con
las etiquetas de cuatro letras **invertidas** — por eso una búsqueda de
`"SCfg"` no encuentra nada y una de `"gfCS"` sí. Es la misma inversión que
usan las entradas del directorio de imágenes del firmware
([01-firmware-ipsw.md](01-firmware-ipsw.md)).

| Offset | Etiqueta | Invertida | Valor |
|---|---|---|---|
| `0x0000` | `gfCS` | `SCfg` | cabecera del bloque |
| `0x0018` | `mNrS` | `SrNm` | el número de serie del aparato |
| `0x0090` | `NBLM` | `MLBN` | el número de placa |
| `0x00a4` | `#doM` | `Mod#` | `MB147` |

`MB147` es justo lo que `libgpod` llama *model number* y con lo que decide qué
iPod está inicializando. Los otros dos son identificativos del aparato del
usuario, así que **no se transcriben aquí**; están en el volcado, que está
fuera de git.

**Lo que NO está en el `SysCfg`: el identificador FireWire.** Se buscó y no
aparece. Ver el apartado 5.

## 3. La receta

`libgpod` viene en Ubuntu pero **sin herramienta de línea de órdenes** que
añada pistas — `libgpod-common` sólo trae `ipod-read-sysinfo-extended` e
`ipod-time-sync`, y `gtkpod` no está empaquetado. Hay que llamar a la
librería. `libgpod-dev` trae `itdb.h`.

```bash
echo "admin" | sudo -S -p "" apt-get install -y libgpod-common libgpod4 libgpod-dev
# python3-gpod NO existe en este Ubuntu: los bindings no son una opción
```

Un programa de cincuenta líneas contra `itdb.h` basta: `itdb_init_ipod()`,
`itdb_parse()`, `itdb_track_new()` + `itdb_track_add()` +
`itdb_playlist_add_track()` a la lista maestra, `itdb_cp_track_to_ipod()`, y
`itdb_write()`.

**`itdb_init_ipod()` falla de entrada** con `Couldn't find the iPod firewire
ID` si no hay `SysInfo`. Hay que escribirlo antes, a mano:

```
iPod_Control/Device/SysInfo
    ModelNumStr: xMB147
    FirewireGuid: 000A270019B4C3D2
    pszSerialNumber: <el del SysCfg>
```

Con eso escribe la biblioteca sin protestar:

```
iPod_Control/Music/F21/libgpod408455.mp3
iPod_Control/iTunes/iTunesDB          6330 bytes, 1 pista
iPod_Control/Artwork/ArtworkDB
iPod_Control/Device/SysInfo
```

Y el disco completo se monta como siempre, con la FAT32 ya poblada:

```bash
python3 tools/ipod_classic/mkdisk6g4k.py ~/6g/Emusic.img \
        assets/extracted/Firmware-24.9.1.2 ~/6g/fat32m.img
```

## 4. Cuidado al arrancarlo a mano

`tools/ipod_classic/navtest.sh` hace `pkill -9 -x qemu-system-arm` antes de
cada carrera, y `-x` es coincidencia **exacta de nombre**. Una copia del
binario con otro nombre sobrevive — cosa útil para que una sesión no mate el
QEMU de otra, y molesta cuando ese superviviente se come un núcleo y hace que
el huésped pierda sus propios plazos de arranque y se reinicie a *diskmode*.

Eso ya costó cuatro carreras, y por eso `navtest.sh` ahora cuenta procesos
sueltos y avisa antes de empezar. Si arrancas una copia con otro nombre para
mirar la pantalla, **mátala tú** antes de fiarte de una carrera de pruebas.

## 5. Por qué no sale la música: el volumen de datos NO se monta

> **SUPERADO el 2026-08-12 — el volumen YA se monta. Ver §6 y §7.** Esta sección
> describía el estado en que el firmware rechazaba la FAT32 en la cabecera. Eso
> **está resuelto** (§6): con tres bytes `"FAT"` en `boot+0x34` el clasificador
> acepta el volumen y el firmware lee el directorio, la FAT y `iPod_Control`
> (medido: 30 lecturas por DMA dentro de la partición contra 2 cuando se
> rechaza). El firmware **ahora sí lee y parsea el `iTunesDB`**; donde se para
> hoy es en la **firma** de esa base de datos (§7) — justo lo que la primera
> versión de §5 sospechaba y la segunda descartó por prematuro. La rueda ha
> dado la vuelta entera. Lo de abajo se conserva como el diagnóstico que llevó
> al §6.

La primera versión de esta sección culpaba a la firma del `iTunesDB`. Resultó
ser **la pregunta correcta hecha demasiado pronto**: el firmware no llegaba a
mirar la base de datos porque **rechazaba el volumen FAT32 entero al leer su
cabecera**, mucho antes de la firma. Resuelto el rechazo del volumen (§6), la
firma vuelve a ser el problema (§7).

### Lo que está VERIFICADO, ejecutando

Todo esto es del 2026-08-12, medido, no supuesto:

1. **Se llega al menú de Música.** Navegando con `sendkey` desde el monitor
   (esperar 38 s tras arrancar y espaciar las pulsaciones ~9 s), el retailOS
   entra en Música y dibuja la lista Cover Flow / Playlists / … / Songs con
   **"No Music"** en el panel. Captura mirada, no sólo contada.

2. **La FAT32 se lee UNA vez y se rechaza.** Con `ATATRACE=1` en un arranque
   que llega a Música: de las ~450 lecturas por DMA, **una sola** cae dentro
   de la partición de datos (sus primeros 64 sectores de 512 B = la cabecera),
   y **nunca** se lee el directorio raíz ni `iPod_Control` ni el `iTunesDB`.
   Si el volumen se hubiera montado, el firmware habría leído el directorio
   buscando `iPod_Control`. No lo hace. Se rechaza en la cabecera.

3. **La firma, el hash y el `iTunesDB.p7b` son IRRELEVANTES.** No se llega a
   validar nada de eso porque el volumen no se monta. La sección anterior se
   preocupaba del problema equivocado.

4. **El rechazo no depende de** (todos probados, misma traza de una lectura):
   - tamaño de sector de la FAT: **512, 2048 y 4096** fallan igual;
   - tipo de partición en el MBR: **0x0B (CHS) y 0x0C (LBA)** fallan igual;
   - el campo de sectores ocultos del sector de arranque, puesto a su valor
     correcto.

### Quién lee la cabecera, y la cadena de montaje

Instrumentando el modelo ATA para imprimir el PC y la pila del huésped en la
lectura de la partición de datos (LBA 181568), la lectura sale de una sola
llamada y la cadena, de dentro afuera, es:

```
0x0805a6f4   primitiva de lectura de bloque (registros en 0x38700000)
0x08326518   bucle de lectura de bloques (troceado en sectores)
0x0827a6b0   envoltorio de lectura del sistema de ficheros
0x0824690c   lectura FS -> compara el resultado con 31 en 0x08246948
0x082465xx   DESPACHO VFS: elige el driver por TIPO de sistema de ficheros
             (cmp #1 / #4 / #5, cada uno con su tabla de funciones)
0x0813b20c   tarea de trabajo de RTXC
```

El punto donde se decide aceptar o rechazar está **detrás de la tabla de
funciones** que selecciona `0x082465xx`: el firmware identifica el tipo de
volumen y despacha al driver correspondiente, y ese driver es el que valida (o
descarta) la cabecera FAT.

### Más adentro: la lectura tiene éxito; el rechazo es de contenido

Bajando la cadena a mano (2026-08-12):

- **La lectura del sector de arranque NO falla.** `0x0827a6b0` es la lectura
  FS y devuelve 0 (éxito); su envoltorio `0x0824690c` sólo mapea errores *de
  lectura* (compara el resultado con 31 y 0, códigos de E/S). Así que los bytes
  de la cabecera llegan bien al buffer. Lo que rechaza el volumen es el
  **examen** de esos bytes, no la lectura.

- **El detector de tabla de particiones es `0x0827a980`.** Lee un sector y mira
  su `+0x1FE`: si es `0xAA55` lo trata como **MBR** y salta al parser
  `0x080aee04`; si empieza por `"ER"` lo trata como **APM** (mapa de particiones
  de Apple) y salta a `0x080a38cc`; si ninguno, error. Las direcciones de los
  dos parsers coinciden con las que la §15 del [11](11-classic6g-retailos.md)
  ya había visto ejecutarse.

- El examen que decide FAT-válido-o-no está **detrás del despacho por vtable**
  de `0x082465xx` (elige driver por tipo, `cmp #1/#4/#5`), y ahí es donde se
  cortó esta sesión: resolver ese puntero en ejecución necesita el depurador.

### Lo que falta, acotado para retomarlo

Poner un breakpoint (con `tools/ipod_classic/gdbpoke.py`) en el driver de FAT
que la vtable selecciona en `0x082465xx`, volcar el buffer del sector de
arranque ya leído, y ver **qué campo rechaza**. Es una tarea de una sola
sesión. Candidatos a mirar de camino: el byte que `0x0824659c` lee en `+6` y
compara con `8`, la cadena `"FAT32   "` en `+0x52` del sector de arranque, y —
por descartar— si el firmware corre el detector `0x0827a980` sobre la propia
partición de datos y, al ver el `0xAA55` que todo sector de arranque FAT lleva,
la confunde con un MBR anidado.

Instrumentos que ayudaron y se pueden rehacer (fueron temporales, no están en
el árbol): en `ipod_classic_ata.c`, imprimir en `dma_go` el PC y la pila del
huésped (`current_cpu` → `ARM_CPU(cs)->env.regs[13..15]`,
`cpu_memory_rw_debug`) cuando `ide_get_sector(is) >= 181568`; eso dio la cadena
de montaje entera de un tiro.

> **Hipótesis de trabajo, sin verificar:** que un iPod classic 6G de fábrica
> formatea su partición de datos con parámetros que nuestra `mkfs.vfat` no
> reproduce (alineación, tamaño de FAT, o un campo del BPB que el driver de
> Apple exige). La forma de zanjarlo es el breakpoint de arriba, no seguir
> probando formatos a ciegas: 512/2048/4096 y 0x0B/0x0C ya se descartaron.

### Segunda incógnita, independiente: que SUENE

Aunque la pista aparezca en la lista, reproducirla es otro problema. El I²S no
está modelado —hoy se tiran al suelo unos 6 000 accesos por arranque— así que
el audio es un frente aparte. Ver [04-roadmap.md](04-roadmap.md).

---

## 6. El volumen SÍ se monta: el truco del `"FAT"` en `+0x34` (2026-08-12)

Todo **VERIFICADO ejecutando**. Base VA = fichero + `0x07FF51B8`.

### La cadena del rechazo, desensamblada y medida

El firmware rechazaba la FAT32 **porque su sector de arranque lleva `0x55AA` en
`+0x1FE`**, la firma que todo VBR de FAT trae. El clasificador genérico de
volúmenes `0x0808253c` lee el bloque 0 de la partición, ve ese `0x55AA` y la
trata como si pudiera ser una **tabla de particiones (MBR)** — no como un
sistema de ficheros:

```
0x080825d4  ldrb ...[r4,#1] / [r4] ; buf[0..1]  (¿"ER" = mapa de particiones Apple?)
0x080825f4  ldrh r0,[r4,#0x1FE]    ; buf[0x1FE]
0x080825f8  cmp  r0,#0xAA55        ; -> r6 = 2 (tipo "sector con 0xAA55")
0x080826d8  bl   0x0806a128        ; handler del "sector con 0xAA55"
0x080826f4  mov  r0,#31            ; RECHAZO del clasificador
```

`0x0806a128` intenta parsear el MBR (cuatro entradas en `buf+0x1BE`); en un VBR
de FAT esa zona es **todo ceros**, así que no hay entrada válida y cae a un
último recurso ASCII:

```
0x0806a1f8  bl 0x800f484 ; memcmp(buf+0x34, "FAT", 3)   -> beq ÉXITO
0x0806a210  bl 0x800f484 ; memcmp(buf+0x2b, "FAT", 3)
0x0806a218  bne 0x0806a240                              ; <<< AQUÍ RECHAZA
0x0806a240  mov r0,#0    ; return 0 -> clasificador devuelve 31 -> "No Music"
```

`mkfs.vfat` deja `+0x34` y `+0x2b` a cero (el `"FAT32   "` de verdad está en
`+0x52`, que este camino **no mira**), así que los dos `memcmp` fallan. La
cadena arriba de `0x0806a128`: `0x0827a6b0` (lectura FS) → `0x0824690c`
(compara con 31) → despacho VFS `0x082465xx`. El detector `0x0827a980` (la
hipótesis del "MBR anidado") corre sólo sobre LBA0 al arrancar; el "MBR
anidado" de verdad ocurre en `0x0808253c`/`0x0806a128` corriendo sobre la
**propia partición de datos**.

### El arreglo, y la prueba de que monta

Escribir tres bytes `46 41 54` (`"FAT"`) en **`boot+0x34`** (zona
`BPB_Reserved`, reservada e ignorada por cualquier driver FAT real) hace que
`0x0806a128` devuelva éxito y el clasificador pase de **rechazar (31)** a
**aceptar (0)**. Lo hace `mkdisk6g4k.py` ahora (`mark_fat_volume`), en el sector
de arranque y en su copia (`BkBootSec`).

**Medido con un instrumento temporal** (LBA de cada DMA en `dma_go`, ya
revertido), arranque **sin teclas**:

| Disco | Lecturas dentro de la partición de datos (LBA512 ≥ 181568) |
|---|---|
| Sin el truco (`E4k.img`) | **2**, las dos en LBA 181568 (sólo el sector de arranque). Rechazado. |
| Con el truco (`E4kfat.img`) | **30**: FAT (181824), directorio raíz (183872), `iPod_Control`, zona del `iTunesDB` (200544), **y escrituras de vuelta**. Montado. |

Un volumen montado lee el directorio; el rechazado no. 30 contra 2 lo zanja.

> **Corrección del auditor (2026-08-12).** Los discos `E4k.img` y `E4kfat.img`
> de la tabla de arriba **no** se diferencian sólo en el marcador: `cmp -l` da
> **518 bytes distintos** (contenido de la FAT/directorio), porque se
> fabricaron de imágenes FAT distintas. Es decir, ese A/B **no aísla** la
> variable, aunque la conclusión sea correcta. Rehecho con un A/B limpio —dos
> discos construidos de la **misma** `fat4k.img`, uno con el marcador y otro con
> esos 6 bytes puestos a cero, **sólo 6 bytes de diferencia**— y medido igual
> (LBA de cada DMA en `dma_go`, arranque sin teclas, 60 s): **marcado = 85
> lecturas** dentro de la partición (181568 arranque, 181824 FAT, 183872
> directorio raíz, 200520–200552 `iTunesDB`); **sin marcar = 3 lecturas, todas
> en 181568** (sólo el sector de arranque). El marcador es la única causa. Los
> LBA coinciden con los de arriba; sólo el conteo cambia por la ventana de
> medida.

> **Matiz honesto.** El `"FAT"@0x34` **no** es lo que escribe Apple: sus dos
> plantillas de formato en `0x0884c3c4`/`0x0884c5c4` (OEM `MSDOS5.0`, etiquetas
> `iPod`/`NO NAME`) tienen `+0x34` a cero (VERIFICADO, volcado). Es un cambio de
> **layout de disco** que satisface el clasificador de Apple, sancionado por el
> encargo. Por qué el firmware sondea la partición con `0x0808253c` en vez de
> montarla por el tipo `0x0B` del MBR de LBA0 queda como pregunta abierta (la
> palanca está aguas arriba de `0x0806a128`), pero para el objetivo —que el
> volumen se monte— esto está medido y funciona.

## 7. Dónde se para HOY: la firma del `iTunesDB` (2026-08-12)

Con el volumen montado, el firmware **lee y parsea** el `iTunesDB`. La base de
datos que escribe `libgpod` es estructuralmente correcta (VERIFICADO,
`mhbd` → `mhsd` tipo 1 con `mhlt` count=1 y un `mhit`, referenciado por la
lista maestra `mhyp`/`mhip`). Pero el parseo **falla en la validación de la
firma** y la biblioteca sale vacía ("No Music", captura mirada en el menú
principal: nota musical + "No Music" en el panel).

Medido con `gdbpoke.py`/`fatprobe.py` (base VA = fichero + `0x07FF51B8`):

| Dirección | Qué es |
|---|---|
| `0x08041360` | parser de secciones del `iTunesDB` (magic `"mhsd"` en `0x8041600`, bucle sobre `r4[0x2c]` secciones) |
| `0x08041364` | `bl 0x08082810` — capta el GUID FireWire (8 bytes), lo *byte-swap*ea a big-endian y lo cachea en `device+0x400f4` |
| `0x08041384` | **`bl 0x08073cf8`** — **el validador de la firma**; usa la zona del GUID (`device+0x400f0/f4`) |
| `0x08041388` | medido: **`r0 = 0xffff5bda`** (error) al volver de `0x08073cf8` |
| `0x080415a8` | salida del parser con `sl = 0xffff5bda` → biblioteca vacía |

**De dónde sale el GUID, y por qué es 0.** El getter `0x08323ac8` lee
`obj[0x38]`(bajo)/`obj[0x3c]`(alto) de un objeto `"SysI"` (`obj` por defecto
`0x0896d6fc`). Ese objeto vive en `.bss` (más allá del final de `osos`, que mide
`0x976A3C`) y **nadie lo puebla**: el arranque es por `-kernel` del `osos`
descifrado, **sin la NOR**, que es donde el aparato real guarda su identidad.
Medido en el contexto de hash (`sl="mhbd"`, `r1="guid"`): el getter devuelve
`r0=0, r1=0`. **El GUID del firmware emulado es `0x0000000000000000`.**

**Y no es (sólo) el valor del GUID.** Se construyó un disco `E4kG.img` cuyo
`iTunesDB` `libgpod` firmó con `FirewireGuid: 000A270019B4C3D2`, y se **forzó**
por gdb el getter para devolver ese mismo GUID (`r0=0x19B4C3D2, r1=0x000A2700`)
en cada llamada. El parser **falló idéntico** (`sl=0xffff5bda`). Es decir: hacer
coincidir el *valor* del GUID no basta. La firma que produce la `libgpod` de
este Ubuntu (4.3.2) para el modelo `MB147` **no satisface a este `retailOS`
concreto (24.9.1.2)** — sea por una diferencia de algoritmo/versión, por la
codificación exacta del GUID que entra al hash (el firmware lo *byte-swap*ea en
`0x08082810`), o por un segundo hash que falta. Esta es la protección que
[§1](#1-por-qué-no-basta-con-copiar-el-mp3) describe: la que "durante años
impidió que las herramientas de terceros escribieran música" en estos modelos.

### Cómo retomarlo (acotado)

El camino independiente de algoritmo: poner un breakpoint dentro de
`0x08073cf8` en el `memcmp`/comparación final, **leer la firma que el firmware
ESPERA** (no la que trae el disco), y escribir esos bytes en el campo de firma
del `iTunesDB` (el campo suele excluirse de su propio hash). `0x08073cf8`
reserva memoria (`0x80c6614`), procesa la zona del GUID con `0x0803f4f0`
(`device+0x400f0`, `0x6c` bytes) y de ahí sale el `0xffff5bda` — ése es el
subárbol a diseccionar. Alternativa de fondo: modelar la NOR/`SysInfo` para que
el firmware tenga un GUID real y **medir** si con eso `libgpod` (alimentada con
el mismo GUID) produce una firma aceptada — el experimento de forzado por gdb
sugiere que no bastaría, pero no descarta un problema de codificación del GUID.

### Herramientas nuevas (no tocan el modelo)

`tools/ipod_classic/fatprobe.py` (breakpoints con volcado relativo a registro),
`fatrun.sh` (arranca QEMU `-S -gdb` y conduce `fatprobe`), `navsongs.sh`
(navega a Music→Songs con captura en cada paso), `navmusic.sh`. El disco que
monta y trae la biblioteca: `~/6g/E4kfat.img` (GUID 0) y `~/6g/E4kG.img`
(GUID `000A2700…`). El truco de `+0x34` ya lo aplica `mkdisk6g4k.py`.

## 8. Resuelto: la firma que este firmware espera son 20 ceros, porque la SHA‑1 es hardware sin modelar (2026‑08‑12)

**La canción ya aparece listada en Música → 曲 (Songs).** Captura mirada:
`~/6g/song_listed/53-songs.png` y `54-songs2.png` — la lista muestra la pista con
su título (el nombre del MP3, en marquesina: «…Encore (Official Audio) - Linkin
Park _ JAY‑Z…») y, debajo, el artista **Linkin Park**. Base VA = fichero +
`0x07FF51B8`.

El §7 dejó dos sospechas —«el algoritmo de `libgpod` no es el que espera el
retailOS» y «es el valor del GUID»—. **Las dos son falsas.** Medido:

- **Sólo cuenta `hash58`.** Sin `iTunesDB.p7b` (no existe en el disco) el
  validador `0x08073cf8` toma el camino `hash58` y ninguno más. VERIFICADO
  (`~/6g/sigprobe1.txt`).
- **El algoritmo del firmware ES `itdb_hash58` de `libgpod`, byte a byte.** Las
  dos S‑boxes (`table1`/`table2`) y el magic `fixed[18]` de la derivación de
  clave están literalmente en `osos_decrypted.bin`, contiguos y en orden
  (`0x08388a10` / `0x08388b10` / `0x08388c10`); y el validador cero‑ea *los
  mismos tres campos* antes de hashear —`db_id` (`+0x18`, 8 B), `unk_0x32`
  (`+0x32`, 20 B), `hash58` (`+0x58`, 20 B)— que `itdb_hash58.c:265‑267`. Es
  HMAC‑SHA1 con clave derivada del GUID FireWire. VERIFICADO (desensamblado +
  fuente `libgpod` 0.8.3).

**La causa real del muro:** la SHA‑1 del S5L8702 **no es cómputo del firmware,
es un acelerador hardware en `0x38000000`** (`SHA1_MEM_BASE`; su driver es
`0x08068b64`: `r6=0x38000000`, escribe control `[r6+0x80]`, puntero `[r6+0x84]`,
longitud `[r6+0x8c]`, comando `[r6]`, y hace *busy‑poll* de `[r6]&1`). **QEMU no
lo modela:** está en la tabla *«peripherals we know the address of but have not
modelled»* de `hw/arm/ipod_classic/ipod_classic.c` (`{ "s5l8702.sha1",
SHA1_MEM_BASE, 0x100000 }`), y sus lecturas devuelven 0. Consecuencia: **cada
digest SHA‑1 que pide el firmware sale a cero** → la clave HMAC sale 0 → **la
firma que el firmware ESPERA colapsa a 20 bytes cero**. VERIFICADO por los dos
lados (traza del huésped + fuente de QEMU).

**La medida decisiva** (`~/6g/sigprobe2.txt`, en el `memcmp` final `0x08328598`):
la firma que el firmware **calcula/espera** = `00 00 … 00` (20 ceros); la que
trae el disco (la que `libgpod` escribió para el GUID del `SysInfo`) =
`28 db 9f 20 …`. Por eso `E4kfat.img` —firmado *correctamente* por `libgpod`— se
rechaza: el firmware nunca llega a computar `28db…`, computa ceros. Y por eso el
forzado por gdb del §7 no cambió nada: con la SHA a 0 la clave es 0 **sea cual
sea el GUID** (aparte de que aquel forzado escribía `r0/r1`, cuando el getter
`0x08082810` deja el GUID en memoria `device+0x400f4`, no en registros).

**El arreglo, VERIFICADO end to end:** poner `mhbd+0x58 = 20 ceros` en las dos
copias del `iTunesDB`. El retorno del validador en `0x08041388` pasa de
`0xffff5bda` a `r0=0`; el parser `0x080415a8` sale con éxito; el panel de Música
pasa de «No Music» a «1 曲/Song»; y la lista Música→曲 muestra la pista. Lo
aplica ahora `mkdisk6g4k.py` (`zero_itunesdb_hash58`, junto al truco `"FAT"@0x34`).
Disco reproducido por el pipeline y comprobado: `~/6g/E4kzero_rebuilt.img`
(construido con `mkdisk6g4k.py … Firmware‑24.9.1.2 fat4k.img`).

> **Stub de conveniencia, anotado** (regla del encargo). Los 20 ceros **no** son
> lo que firma un iPod de fábrica; son la única firma que este emulador acepta
> *mientras* `0x38000000` no esté modelado. Está escrito así en
> `zero_itunesdb_hash58()`. Cuando se modele la SHA‑1 hay que quitarlo y firmar
> con `libgpod` para el GUID real del aparato.

### El otro muro de esta sesión: entrar de verdad a la lista (teclas)

El conteo «1 Song» ya salía; **entrar** a la lista fallaba por la pérdida de
SELECT (wiki/11 §18.8/§19.6). Medido aquí, y es la clave para navegar fiable:
**una pulsación de centro «en frío» —sin actividad de rueda previa— el retailOS
la IGNORA**; sólo honra SELECT con la rueda activa (recién tocada). Con eso la
navegación se vuelve determinista: **«girar para tocar la rueda, LUEGO pulsar
centro»**, y verificar cada paso con un *diff* de píxeles de dos `screendump`.
Como Música es la fila de arriba y los menús no dan la vuelta, un `q` (subir) la
toca sin moverse de ella; para llegar a 曲 se baja cinco pasos.

### Herramientas nuevas (no tocan el modelo)

`tools/ipod_classic/iremote.py` (control por el monitor TCP: `press`/`seq` con
verificación por *diff*), `irun.sh` (arranca una copia `qmus` —nombre propio,
para que el `pkill -x` de `navtest.sh` no la toque— con monitor en
`127.0.0.1:4455`) e `isongs.sh` (conduce idioma→menú→Música→曲 con captura por
paso). Sólo leen/teclean; **no modifican el modelo ni recompilan QEMU** (el
arreglo de la firma es de disco, no de emulación).

### Camino de fondo, pendiente (INFERIDO)

Modelar el acelerador SHA‑1 en `0x38000000` (referencia: el Touch ya lo hace en
`hw/arm/ipod_touch_sha1.c`). Con la SHA real el firmware computaría un HMAC‑SHA1
no trivial, y entonces `hash58` habría que firmarlo con el **GUID real** del
aparato —hoy `0`, porque se arranca por `-kernel` sin la NOR/`SysInfo`—. Como el
algoritmo coincide, `libgpod` firmaría bien *una vez* que (a) la SHA esté
modelada y (b) el GUID del modelo y el del `SysInfo` sean el mismo. No probado —
es la vía si se quiere fidelidad al hardware en vez del atajo de los 20 ceros.
